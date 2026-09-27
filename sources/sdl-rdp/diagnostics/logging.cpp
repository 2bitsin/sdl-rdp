#include <sdl-rdp/diagnostics/logging.hpp>

#include <sdl-rdp/diagnostics/exceptions.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <oxbox/utilities/number-text.hpp>
#include <winpr/wlog.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string_view>

namespace sdl_rdp::diagnostics::detail::logging {
using sdl_rdp::freerdp_facade::NumberKey;
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Unreachable;

auto ResetAuthenticationLogging() -> void {
  LogRoute::WithFilter([](auto& filter) { filter = { }; });
}
auto PeerNegotiationLogging(SettingsReader settings) -> void {
  LogRoute::WithFilter([&](auto& filter) { filter.peer_settings = settings; });
}
auto NegotiationRefused() -> bool {
  return LogRoute::WithFilter([](auto const& filter) { return filter.negotiation_failed; });
}
auto TlsHandshakeFailed() -> bool {
  return LogRoute::WithFilter([](auto const& filter) { return filter.handshake_failed; });
}
auto AuthenticationRejectedLogging() -> void {
  LogRoute::WithFilter([](auto& filter) { filter.authentication_failed = true; });
}
auto ExpectedDisconnect(Cause cause) -> bool {
  switch (cause) {
  case Cause::TransportFailed:
  case Cause::LogoffByUser:
  case Cause::OtherConnection:
  case Cause::RpcInitiated:
  case Cause::AuthenticationFailed:
  case Cause::ServerDenied: return true;
  case Cause::None:
  case Cause::Other: return false;
  default:           Unreachable(cause);
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
auto ExpectedLibraryMessage(std::string_view prefix, std::string_view text) -> bool {
  if (std::ranges::any_of(KnownLibraryMessages,
                          [&](auto const& entry) { return prefix == entry.first && text == entry.second; }))
    return true;
  if (prefix == "com.freerdp.core.transport") {
    constexpr std::string_view system_error = "BIO_read returned a system error ";
    if (!text.starts_with(system_error)) return false;
    text.remove_prefix(system_error.size());
    auto const colon = text.find(": ");
    if (colon == std::string_view::npos) return false;
    return oxbox::utilities::ParseNumber<std::uint32_t>(text.substr(0, colon)).has_value();
  }
  return false;
}
auto ErrorName(std::string_view text) -> std::string_view {
  return text.substr(0, text.find(' '));
}
auto SspiRejectionEcho(LogRoute::Filter const& filter, std::string_view prefix, std::string_view text) -> bool {
  static constexpr std::array<std::string_view, 2> sspi{
    "AcceptSecurityContext status SEC_E_MESSAGE_ALTERED [0x8009030f]",
    "AcceptSecurityContext status SEC_E_NO_CREDENTIALS [0x8009030e]"
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
auto DetectTlsHandshakeFailure(LogRoute::Filter& filter, std::string_view prefix, std::string_view text) -> bool {
  if (!filter.peer_settings || prefix != "com.freerdp.crypto" || text != "BIO_do_handshake failed") return false;
  if (filter.peer_settings->Get(NumberKey::SelectedProtocol) != SecurityTls) return false;
  filter.handshake_failed = true;
  return true;
}
auto NegotiationEcho(LogRoute::Filter const& filter, std::string_view prefix, std::string_view text) -> bool {
  static constexpr std::array<std::string_view, 6> echoes{
    "server supports only", "Protocol security negotiation fail",
    "BIO_do_handshake failed", "rdp_server_accept_nego() fail",
    "STATE_RUN_FAILED", "ERRCONNECT_CONNECT_TRANSPORT_FAILED"
  };
  return (filter.negotiation_failed || filter.handshake_failed)
         && (prefix.starts_with("com.freerdp.core") || prefix == "com.freerdp.api" || prefix == "com.freerdp.crypto")
         && std::ranges::any_of(echoes, [&](auto entry) { return text.contains(entry); });
}
auto TransportEcho(std::string_view prefix, std::string_view text) -> bool {
  static constexpr std::array<std::string_view, 3> disconnects { "ERRINFO_LOGOFF_BY_USER",
                                                                 "ERRINFO_DISCONNECTED_BY_OTHER_CONNECTION",
                                                                 "ERRINFO_RPC_INITIATED_DISCONNECT" };
  auto const                                       name        = ErrorName(text);
  if ((prefix == "com.freerdp.core" || prefix == "com.freerdp.core.peer")
      && name == "ERRCONNECT_CONNECT_TRANSPORT_FAILED")
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
  return (prefix == "com.winpr.sspi.NTLM" && std::ranges::contains(ntlm, text))
         || (prefix == "com.freerdp.core.nla"
             && std::ranges::any_of(nla, [&](auto entry) { return text.starts_with(entry); }))
         || std::ranges::contains(rejected, ErrorName(text));
}
auto AuthenticationCoreEcho(LogRoute::Filter const& filter, std::string_view prefix, std::string_view text) -> bool {
  return filter.authentication_failed && (prefix.starts_with("com.freerdp.core") || prefix == "com.freerdp.api")
         && std::ranges::any_of(core, [&](auto entry) { return text.contains(entry); });
}
auto AuthenticationEcho(LogRoute::Filter& filter, std::string_view prefix, std::string_view text) -> bool {
  if (!AuthenticationFailure(prefix, text)) return AuthenticationCoreEcho(filter, prefix, text);
  filter.authentication_failed = true;
  return true;
}
auto ExpectedPeerMessage(LogRoute::Filter& filter, wLogMessage const& message) -> bool {
  if (!message.PrefixString || !message.TextString) return false;
  auto prefix = std::string_view(message.PrefixString);
  auto text   = std::string_view(message.TextString);
  return ExpectedLibraryMessage(prefix, text) || SspiRejectionEcho(filter, prefix, text)
         || DetectNegotiationRefusal(filter, prefix, text) || DetectTlsHandshakeFailure(filter, prefix, text)
         || NegotiationEcho(filter, prefix, text) || TransportEcho(prefix, text)
         || AuthenticationEcho(filter, prefix, text);
}
auto NtlmMessage(wLogMessage const& message) -> bool {
  return message.PrefixString && std::string_view(message.PrefixString) == "com.winpr.sspi.NTLM";
}
auto LibraryLevel(std::uint32_t level) -> LogLevel {
  if (level == WLOG_ERROR) return LogLevel::Error;
  return level == WLOG_WARN ? LogLevel::Warn : LogLevel::Info;
}
}
auto LogRoute::Forward(wLogMessage const& message) -> void {
  auto&                  routing = Shared();
  std::scoped_lock const lock(routing.guard);
  auto&                  filter  = routing.filters[std::this_thread::get_id()];
  if (message.Level < WLOG_INFO || message.Type != WLOG_MESSAGE_TEXT) return;
  auto const expected = ExpectedPeerMessage(filter, message);
  // SSPI debug output can contain credentials and hashes, including binary dump callbacks.
  if (NtlmMessage(message) && !expected) return;
  auto const level = expected ? LogLevel::Info : LibraryLevel(message.Level);
  if (routing.active) routing.active->get().Deliver(level, message);
}
auto LogRoute::Deliver(LogLevel level, wLogMessage const& message) const -> void {
  if (message.TextString) _sink.get().Log(level, message.TextString);
}
auto LogRoute::Log(LogLevel level, std::string_view text) const -> void {
  _sink.get().Log(level, text);
}
auto LogRoute::Install() -> void {
  auto* root = WLog_GetRoot();
  Expects(root != nullptr, "WLog root exists");
  // abi: wLogCallbackMessage_t and its siblings, BOOL is int
  constexpr auto forward   = [](wLogMessage const* message) noexcept -> int {
    Expects(message != nullptr, "WLog message exists");
    auto const forwarded = [&] {
      Forward(*message);
      return true;
    };
    // The log route is the only reporting channel, so its own failure has nowhere further to go.
    return Contained(false, forwarded, [](std::string_view) noexcept { });
  };
  wLogCallbacks  callbacks { forward, forward, forward, forward };
  if (!WLog_SetLogAppenderType(root, WLOG_APPENDER_CALLBACK)
      || !WLog_ConfigureAppender(WLog_GetLogAppender(root), "callbacks", &callbacks))
    throw LogCallbackFailed{ };
  WLog_Layout_SetPrefixFormat(root, WLog_GetLogLayout(root), "%mn");
  if (auto* level = std::getenv("WLOG_LEVEL"))
    WLog_SetStringLogLevel(root, level);
  else
    WLog_SetLogLevel(root, WLOG_WARN);
}
auto LogRoute::Shared() -> LogRoute::Routing& {
  // WLog has no user pointer; this owner lasts as long as its process-wide callback.
  static Routing routing;
  return routing;
}
LogRoute::LogRoute(LogSink& sink) : _sink{ sink } {
  auto& routing = Shared();
  std::call_once(routing.installed, Install);
  std::scoped_lock const lock(routing.guard);
  routing.active = std::cref(*this);
}
LogRoute::~LogRoute() {
  auto&                  routing = Shared();
  std::scoped_lock const lock(routing.guard);
  if (routing.active && &routing.active->get() == this) routing.active.reset();
}
}
