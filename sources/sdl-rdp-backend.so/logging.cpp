#include "_detail/logging.hpp"
#include "_detail/contract.hpp"
#include <winpr/wlog.h>
#include <atomic>
#include <charconv>
#include <array>
#include <algorithm>
#include <mutex>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <freerdp/error.h>
#include <freerdp/settings.h>

namespace Backend {
namespace {
// WLog has no peer context; Serve supplies settings and resets these filters at both boundaries.
thread_local bool authentication_failed = false;
thread_local rdpSettings const* peer_settings = nullptr;
thread_local bool negotiation_failed = false;
thread_local bool handshake_failed = false;
}
void ResetAuthenticationLogging() { authentication_failed = false; peer_settings = nullptr; negotiation_failed = false; handshake_failed = false; }
void PeerNegotiationLogging(rdpSettings const* settings) { peer_settings = settings; }
bool NegotiationRefused() { return negotiation_failed; }
bool TlsHandshakeFailed() { return handshake_failed; }
void AuthenticationRejectedLogging() { authentication_failed = true; }
bool ExpectedDisconnect(unsigned code)
{
  return code == FREERDP_ERROR_CONNECT_TRANSPORT_FAILED || code == FREERDP_ERROR_LOGOFF_BY_USER
    || code == FREERDP_ERROR_DISCONNECTED_BY_OTHER_CONNECTION
    || code == FREERDP_ERROR_RPC_INITIATED_DISCONNECT || code == FREERDP_ERROR_AUTHENTICATION_FAILED
    || code == FREERDP_ERROR_SERVER_DENIED_CONNECTION;
}
namespace {
// The newest open owns process-wide WLog routing; close waits for callbacks before clearing it.
std::atomic<LogRoute*> route = nullptr;
std::recursive_mutex routing_guard;
bool ExpectedLibraryMessage(std::string_view prefix, std::string_view text)
{
  constexpr std::pair<std::string_view, std::string_view> known[] = {
    {"com.freerdp.core.transport", "BIO_read retries exceeded"},
    {"com.freerdp.core.transport", "BIO_should_retry returned an error: error:80000068:system library::Connection reset by peer"},
    {"com.freerdp.core.transport", "BIO_write returned a system error 32: Broken pipe"},
    {"com.freerdp.core.transport", "BIO_should_retry returned an error: error:80000020:system library::Broken pipe"},
    {"com.freerdp.channels.rdpsnd.server", "client doesn't support any format!"},
  };
  for (auto const& [source, message_text] : known)
    if (prefix == source && text == message_text) return true;
  if (prefix == "com.freerdp.core.transport") {
    constexpr std::string_view system_error = "BIO_read returned a system error ";
    if (!text.starts_with(system_error)) return false;
    text.remove_prefix(system_error.size());
    unsigned error = 0;
    auto [end, status] = std::from_chars(text.data(), text.data() + text.size(), error);
    return status == std::errc{} && std::string_view(end, text.data() + text.size()).starts_with(": ");
  }
  return false;
}
bool SspiRejectionEcho(std::string_view prefix, std::string_view text)
{
  static constexpr std::array<std::string_view, 2> sspi{
    "AcceptSecurityContext status SEC_E_MESSAGE_ALTERED [0x8009030F]",
    "AcceptSecurityContext status SEC_E_NO_CREDENTIALS [0x8009030E]"};
  if (authentication_failed && prefix == "com.winpr.sspi" &&
      std::ranges::any_of(sspi, [&](auto entry) { return text == entry; })) return true;
  return false;
}
bool DetectNegotiationRefusal(std::string_view prefix, std::string_view text)
{
  static constexpr std::array<std::string_view, 4> refusal{
    "server supports only Standard RDP Security", "server supports only NLA Security",
    "server supports only a SSL based Security (TLS or NLA)", "Protocol security negotiation failure"};
  if (!peer_settings || prefix != "com.freerdp.core.connection") return false;
  if (!std::ranges::any_of(refusal, [&](auto entry) { return text == entry; })) return false;
  negotiation_failed = true;
  return true;
}
bool DetectTlsHandshakeFailure(std::string_view prefix, std::string_view text)
{
  if (!peer_settings || prefix != "com.freerdp.crypto" || text != "BIO_do_handshake failed") return false;
  if (freerdp_settings_get_uint32(peer_settings, FreeRDP_SelectedProtocol) != SecurityTls) return false;
  handshake_failed = true;
  return true;
}
bool NegotiationEcho(std::string_view prefix, std::string_view text)
{
  static constexpr std::array<std::string_view, 6> echoes{
    "server supports only", "Protocol security negotiation failure", "BIO_do_handshake failed",
    "rdp_server_accept_nego() fail", "STATE_RUN_FAILED", "ERRCONNECT_CONNECT_TRANSPORT_FAILED"};
  return (negotiation_failed || handshake_failed)
    && (prefix.starts_with("com.freerdp.core") || prefix == "com.freerdp.api" || prefix == "com.freerdp.crypto")
    && std::ranges::any_of(echoes, [&](auto entry) { return text.contains(entry); });
}
bool TransportEcho(std::string_view prefix, std::string_view text)
{
  auto name = text.substr(0, text.find(' '));
  if ((prefix == "com.freerdp.core" || prefix == "com.freerdp.core.peer")
      && name == "ERRCONNECT_CONNECT_TRANSPORT_FAILED") return true;
  return prefix == "com.freerdp.core.peer" && (name == "ERRINFO_LOGOFF_BY_USER"
    || name == "ERRINFO_DISCONNECTED_BY_OTHER_CONNECTION" || name == "ERRINFO_RPC_INITIATED_DISCONNECT");
}
bool AuthenticationEcho(std::string_view prefix, std::string_view text)
{
  static constexpr std::array<std::string_view, 2> ntlm{
    "Message Integrity Check (MIC) verification failed!", "NtProofString verification failed!"};
  static constexpr std::array<std::string_view, 1> nla{"SPNEGO failed with NTSTATUS:"};
  static constexpr std::array<std::string_view, 8> core{
    "STATE_RUN_FAILED", "rdp_server_accept_nego() fail", "freerdp_post_connect failed",
    "AcceptSecurityContext", "nla_recv_pdu() fail", "client authentication failure",
    "freerdp_peer::Capabilities() callback failed", "freerdp_peer::Activate() callback failed"};
  if ((prefix == "com.winpr.sspi.NTLM" && std::ranges::any_of(ntlm, [&](auto entry) { return text == entry; })) ||
      (prefix == "com.freerdp.core.nla" && std::ranges::any_of(nla, [&](auto entry) { return text.starts_with(entry); }))) {
    authentication_failed = true;
    return true;
  }
  if (authentication_failed && (prefix.starts_with("com.freerdp.core") || prefix == "com.freerdp.api") &&
      std::ranges::any_of(core, [&](auto entry) { return text.contains(entry); })) return true;
  auto name = text.substr(0, text.find(' '));
  if (name == "ERRCONNECT_AUTHENTICATION_FAILED" || name == "ERRCONNECT_LOGON_FAILURE" || name == "ERRINFO_SERVER_DENIED_CONNECTION") { authentication_failed = true; return true; }
  return false;
}
bool ExpectedPeerMessage(wLogMessage const& message)
{
  if (!message.PrefixString || !message.TextString) return false;
  auto prefix = std::string_view(message.PrefixString);
  auto text = std::string_view(message.TextString);
  return ExpectedLibraryMessage(prefix, text) || SspiRejectionEcho(prefix, text)
    || DetectNegotiationRefusal(prefix, text) || DetectTlsHandshakeFailure(prefix, text)
    || NegotiationEcho(prefix, text) || TransportEcho(prefix, text) || AuthenticationEcho(prefix, text);
}
BOOL Forward(wLogMessage const* message)
{
  utilities::Expects(message != nullptr, "WLog message exists");
  // SSPI debug output can contain credentials and hashes, including binary dump callbacks.
  if (message->Level < WLOG_INFO || message->Type != WLOG_MESSAGE_TEXT) return TRUE;
  if (message->PrefixString && std::string_view(message->PrefixString) == "com.winpr.sspi.NTLM"
      && !ExpectedPeerMessage(*message)) return TRUE;
  std::scoped_lock lock(routing_guard);
  auto target = route.load();
  auto level = message->Level == WLOG_ERROR ? SDLRDP_LOG_ERROR
    : message->Level == WLOG_WARN ? SDLRDP_LOG_WARN : SDLRDP_LOG_INFO;
  if (ExpectedPeerMessage(*message)) level = SDLRDP_LOG_INFO;
  auto text = message->TextString;
  if (target && target->callback && text) target->callback(target->user, level, text);
  return TRUE;
}
void Install()
{
  auto root = WLog_GetRoot();
  utilities::Expects(root != nullptr, "WLog root exists");
  wLogCallbacks callbacks{Forward, Forward, Forward, Forward};
  if (!WLog_SetLogAppenderType(root, WLOG_APPENDER_CALLBACK)
      || !WLog_ConfigureAppender(WLog_GetLogAppender(root), "callbacks", &callbacks))
    throw std::runtime_error("WLog callback installation failed.");
  WLog_Layout_SetPrefixFormat(root, WLog_GetLogLayout(root), "%mn");
  if (auto level = std::getenv("WLOG_LEVEL")) WLog_SetStringLogLevel(root, level);
  else WLog_SetLogLevel(root, WLOG_WARN);
}
}
LogRoute::LogRoute(sdlrdp_config const& config) : callback(config.log), user(config.log_user)
{
  static std::once_flag installed;
  std::call_once(installed, Install);
  std::scoped_lock lock(routing_guard);
  route.store(this);
  utilities::Ensures(route.load() == this, "newest handle owns logging");
}
LogRoute::~LogRoute()
{
  std::scoped_lock lock(routing_guard);
  auto expected = this;
  route.compare_exchange_strong(expected, nullptr);
}
}
