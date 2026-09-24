#include "_detail/logging.hpp"

#include "_detail/contract.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <freerdp/error.h>
#include <freerdp/settings.h>
#include <mutex>
#include <oxbox/utilities/number-text.hpp>
#include <stdexcept>
#include <string_view>
#include <winpr/wlog.h>

namespace Backend {
void ResetAuthenticationLogging() {
  LogRoute::WithFilter([](auto& filter) { filter = { }; });
}
void PeerNegotiationLogging(rdpSettings const* settings) {
  LogRoute::WithFilter([=](auto& filter) { filter.peer_settings = settings; });
}
bool NegotiationRefused() {
  return LogRoute::WithFilter([](auto const& filter) { return filter.negotiation_failed; });
}
bool TlsHandshakeFailed() {
  return LogRoute::WithFilter([](auto const& filter) { return filter.handshake_failed; });
}
void AuthenticationRejectedLogging() {
  LogRoute::WithFilter([](auto& filter) { filter.authentication_failed = true; });
}
auto ExpectedDisconnect(unsigned code) -> bool {
  switch (code) {
  case FREERDP_ERROR_CONNECT_TRANSPORT_FAILED:
  case FREERDP_ERROR_LOGOFF_BY_USER:
  case FREERDP_ERROR_DISCONNECTED_BY_OTHER_CONNECTION:
  case FREERDP_ERROR_RPC_INITIATED_DISCONNECT:
  case FREERDP_ERROR_AUTHENTICATION_FAILED:
  case FREERDP_ERROR_SERVER_DENIED_CONNECTION:
    return true;
  default:
    return false;
  }
}
namespace {
constexpr std::array<std::pair<std::string_view, std::string_view>, 9> KnownLibraryMessages{ {
    { "com.freerdp.core.transport", "BIO_read retries exceeded" },
    { "com.freerdp.core.transport",
      "BIO_should_retry returned an error: error:80000068:system library::Connection reset by peer" },
    { "com.freerdp.core.transport"        , "BIO_write returned a system error 32: Broken pipe" },
    { "com.freerdp.core.transport", "BIO_should_retry returned an error: error:80000020:system library::Broken pipe" },
    { "com.freerdp.channels.rdpsnd.server", "client doesn't support any format!"                },
    { "com.freerdp.core"                  , "setsockopt() IPPROTO_TCP, TCP_KEEPIDLE"            },
    { "com.freerdp.core"                  , "setsockopt() SOL_TCP, TCP_KEEPCNT"                 },
    { "com.freerdp.core"                  , "setsockopt() SOL_TCP, TCP_KEEPINTVL"               },
    { "com.freerdp.core"                  , "setsockopt() SOL_TCP, TCP_USER_TIMEOUT"            },
} };
bool ExpectedLibraryMessage(std::string_view prefix, std::string_view text) {
  if (std::ranges::any_of(KnownLibraryMessages,
                          [&](auto const& entry) { return prefix == entry.first && text == entry.second; }))
    return true;
  if (prefix == "com.freerdp.core.transport") {
    constexpr std::string_view system_error = "BIO_read returned a system error ";
    if (!text.starts_with(system_error)) return false;
    text.remove_prefix(system_error.size());
    auto const colon = text.find(": ");
    if (colon == std::string_view::npos) return false;
    return oxbox::utilities::ParseNumber<unsigned>(text.substr(0, colon)).has_value();
  }
  return false;
}
auto ErrorName(std::string_view text) -> std::string_view {
  return text.substr(0, text.find(' '));
}
auto SspiRejectionEcho(LogRoute::Filter const& filter, std::string_view prefix, std::string_view text) -> bool {
  static constexpr std::array<std::string_view, 2> sspi{
    "AcceptSecurityContext status SEC_E_MESSAGE_ALTERED [0x8009030F]",
    "AcceptSecurityContext status SEC_E_NO_CREDENTIALS [0x8009030E]"
  };
  return filter.authentication_failed && prefix == "com.winpr.sspi" && std::ranges::contains(sspi, text);
}
auto DetectNegotiationRefusal(LogRoute::Filter& filter, std::string_view prefix, std::string_view text) -> bool {
  static constexpr std::array<std::string_view, 4> refusal{ "server supports only Standard RDP Security",
                                                            "server supports only NLA Security",
                                                            "server supports only a SSL based Security (TLS or NLA)",
                                                            "Protocol security negotiation failure" };
  if (!filter.peer_settings || prefix != "com.freerdp.core.connection") return false;
  if (!std::ranges::contains(refusal, text)) return false;
  filter.negotiation_failed = true;
  return true;
}
bool DetectTlsHandshakeFailure(LogRoute::Filter& filter, std::string_view prefix, std::string_view text) {
  if (!filter.peer_settings || prefix != "com.freerdp.crypto" || text != "BIO_do_handshake failed") return false;
  if (freerdp_settings_get_uint32(filter.peer_settings, FreeRDP_SelectedProtocol) != SecurityTls) return false;
  filter.handshake_failed = true;
  return true;
}
auto NegotiationEcho(LogRoute::Filter const& filter, std::string_view prefix, std::string_view text) -> bool {
  static constexpr std::array<std::string_view, 6> echoes{
    "server supports only", "Protocol security negotiation failure",
    "BIO_do_handshake failed", "rdp_server_accept_nego() fail",
    "STATE_RUN_FAILED", "ERRCONNECT_CONNECT_TRANSPORT_FAILED"
  };
  return (filter.negotiation_failed || filter.handshake_failed) &&
         (prefix.starts_with("com.freerdp.core") || prefix == "com.freerdp.api" || prefix == "com.freerdp.crypto") &&
         std::ranges::any_of(echoes, [&](auto entry) { return text.contains(entry); });
}
auto TransportEcho(std::string_view prefix, std::string_view text) -> bool {
  static constexpr std::array<std::string_view, 3> disconnects { "ERRINFO_LOGOFF_BY_USER",
                                                                 "ERRINFO_DISCONNECTED_BY_OTHER_CONNECTION",
                                                                 "ERRINFO_RPC_INITIATED_DISCONNECT" };
  auto const                                       name        = ErrorName(text);
  if ((prefix == "com.freerdp.core" || prefix == "com.freerdp.core.peer") &&
      name == "ERRCONNECT_CONNECT_TRANSPORT_FAILED")
    return true;
  return prefix == "com.freerdp.core.peer" && std::ranges::contains(disconnects, name);
}
constexpr std::array<std::string_view, 2> ntlm    { "Message Integrity Check (MIC) verification failed!",
                                                    "NtProofString verification failed!" };
constexpr std::array<std::string_view, 1> nla     { "SPNEGO failed with NTSTATUS:" };
constexpr std::array<std::string_view, 3> rejected{ "ERRCONNECT_AUTHENTICATION_FAILED", "ERRCONNECT_LOGON_FAILURE",
                                                    "ERRINFO_SERVER_DENIED_CONNECTION" };
constexpr std::array<std::string_view, 8> core    { "STATE_RUN_FAILED",
                                                    "rdp_server_accept_nego() fail",
                                                    "freerdp_post_connect failed",
                                                    "AcceptSecurityContext",
                                                    "nla_recv_pdu() fail",
                                                    "client authentication failure",
                                                    "freerdp_peer::Capabilities() callback failed",
                                                    "freerdp_peer::Activate() callback failed" };

auto AuthenticationFailure(std::string_view prefix, std::string_view text) -> bool {
  return (prefix == "com.winpr.sspi.NTLM" && std::ranges::contains(ntlm, text)) ||
         (prefix == "com.freerdp.core.nla" &&
          std::ranges::any_of(nla, [&](auto entry) { return text.starts_with(entry); })) ||
         std::ranges::contains(rejected, ErrorName(text));
}
auto AuthenticationCoreEcho(LogRoute::Filter const& filter, std::string_view prefix, std::string_view text)
    -> bool {
  return filter.authentication_failed && (prefix.starts_with("com.freerdp.core") || prefix == "com.freerdp.api") &&
         std::ranges::any_of(core, [&](auto entry) { return text.contains(entry); });
}
auto AuthenticationEcho(LogRoute::Filter& filter, std::string_view prefix, std::string_view text) -> bool {
  if (!AuthenticationFailure(prefix, text)) return AuthenticationCoreEcho(filter, prefix, text);
  filter.authentication_failed = true;
  return true;
}
bool ExpectedPeerMessage(LogRoute::Filter& filter, wLogMessage const& message) {
  if (!message.PrefixString || !message.TextString) return false;
  auto prefix = std::string_view(message.PrefixString);
  auto text   = std::string_view(message.TextString);
  return ExpectedLibraryMessage(prefix, text) || SspiRejectionEcho(filter, prefix, text) ||
         DetectNegotiationRefusal(filter, prefix, text) || DetectTlsHandshakeFailure(filter, prefix, text) ||
         NegotiationEcho(filter, prefix, text) || TransportEcho(prefix, text) ||
         AuthenticationEcho(filter, prefix, text);
}
auto NtlmMessage(wLogMessage const& message) -> bool {
  return message.PrefixString && std::string_view(message.PrefixString) == "com.winpr.sspi.NTLM";
}
auto LibraryLevel(uint32_t level) -> sdlrdp_log_level {
  if (level == WLOG_ERROR) return SDLRDP_LOG_ERROR;
  return level == WLOG_WARN ? SDLRDP_LOG_WARN : SDLRDP_LOG_INFO;
}
}
// WLog's wLogCallbacks table dictates the BOOL result.
auto LogRoute::Forward(wLogMessage const* message) -> BOOL {
  utilities::Expects(message != nullptr, "WLog message exists");
  auto&                  routing = Shared();
  std::scoped_lock const lock(routing.guard);
  auto&                  filter  = routing.filters[std::this_thread::get_id()];
  if (message->Level < WLOG_INFO || message->Type != WLOG_MESSAGE_TEXT) return TRUE;
  auto const expected = ExpectedPeerMessage(filter, *message);
  // SSPI debug output can contain credentials and hashes, including binary dump callbacks.
  if (NtlmMessage(*message) && !expected) return TRUE;
  auto const level = expected ? SDLRDP_LOG_INFO : LibraryLevel(message->Level);
  if (routing.active) routing.active->Deliver(level, message->TextString);
  return TRUE;
}
auto LogRoute::Deliver(sdlrdp_log_level level, char const* text) const -> void {
  if (callback && text) callback(user, level, text);
}
void LogRoute::Install() {
  auto* root = WLog_GetRoot();
  utilities::Expects(root != nullptr, "WLog root exists");
  wLogCallbacks callbacks{ Forward, Forward, Forward, Forward };
  if (!WLog_SetLogAppenderType(root, WLOG_APPENDER_CALLBACK) ||
      !WLog_ConfigureAppender(WLog_GetLogAppender(root), "callbacks", &callbacks))
    throw std::runtime_error("WLog callback installation failed.");
  WLog_Layout_SetPrefixFormat(root, WLog_GetLogLayout(root), "%mn");
  if (auto* level = std::getenv("WLOG_LEVEL"))
    WLog_SetStringLogLevel(root, level);
  else
    WLog_SetLogLevel(root, WLOG_WARN);
}
LogRoute::Routing& LogRoute::Shared() {
  // WLog has no user pointer; this owner lasts as long as its process-wide callback.
  static Routing routing;
  return routing;
}
LogRoute::LogRoute(sdlrdp_config const& config) : callback(config.log), user(config.log_user) {
  auto& routing = Shared();
  std::call_once(routing.installed, Install);
  std::scoped_lock const lock(routing.guard);
  routing.active = this;
  utilities::Ensures(routing.active == this, "newest handle owns logging");
}
LogRoute::~LogRoute() {
  auto&                  routing = Shared();
  std::scoped_lock const lock(routing.guard);
  if (routing.active == this) routing.active = nullptr;
}
}
