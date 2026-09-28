#include <sdl-rdp/freerdp-facade/connection.hpp>

#include <sdl-rdp/freerdp-facade/button-flags.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/exceptions.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/freerdp-facade/ntlm.hpp>
#include <sdl-rdp/freerdp-facade/record-array.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>
#include <sdl-rdp/utilities/text.hpp>

#include <freerdp/channels/channels.h>
#include <freerdp/error.h>
#include <freerdp/freerdp.h>
#include <freerdp/input.h>
#include <freerdp/peer.h>
#include <freerdp/session.h>
#include <freerdp/transport_io.h>
#include <freerdp/update.h>
#include <oxbox/utilities/span.hpp>
#include <winpr/crypto.h>
#include <winpr/ssl.h>
#include <winpr/sspi.h>
#include <winpr/wtsapi.h>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace sdl_rdp::freerdp_facade::detail::connection {
using sdl_rdp::utilities::AllocationFailed;
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::DescriptorOf;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::NativeOf;
using sdl_rdp::utilities::NtOwf;
using sdl_rdp::utilities::OperationName;
using sdl_rdp::utilities::TranscodeRange;
using sdl_rdp::utilities::Unreachable;
using sdl_rdp::utilities::Utf16;

auto ReleasePeer(rdp_freerdp_peer* peer) noexcept -> void {
  freerdp_peer_context_free(peer);
  freerdp_peer_free(peer);
}
namespace {
// Process-wide and idempotent; OpenSSL 3 releases its state at exit, so neither has a release call.
auto PrepareLibrary() -> void {
  static std::once_flag once;
  std::call_once(once, [] {
    WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi());
    if (!winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT)) throw LibraryInitFailed{ "OpenSSL" };
  });
}
auto Adopted(Socket socket) -> PeerHandle {
  Expects(socket.Owns(), "the connection's socket is open");
  PeerHandle peer{ freerdp_peer_new(DescriptorOf(socket.Native())) };
  if (!peer) throw AllocationFailed{ "Peer" };
  std::ignore = socket.Release();
  return peer;
}
// The transport takes the descriptor from the peer when the context is created and leaves sockfd behind.
auto SocketOf(PeerHandle const& peer) -> NativeSocket {
  Expects(peer != nullptr, "the accepted peer exists");
  return NativeOf(peer->sockfd);
}
auto WithContext(PeerHandle peer) -> PeerHandle {
  Expects(peer != nullptr, "the accepted peer exists");
  PrepareLibrary();
  if (!freerdp_peer_context_new(peer.get())) throw PeerContextFailed{ "Connection" };
  Ensures(peer->context != nullptr, "the peer context exists once created");
  Ensures(peer->context->update != nullptr, "the peer context carries its update table");
  Ensures(peer->context->input != nullptr, "the peer context carries its input table");
  return peer;
}

// WinPR types identity text as BYTE for UTF-8 and UINT16 for UTF-16; the code units are the text.
auto IdentityText(std::span<std::uint16_t const> utf16) -> std::string {
  using oxbox::utilities::Encoding;
  auto const text = std::u16string_view{ oxbox::utilities::SpanCast<char16_t const>(utf16) };
  return TranscodeRange<std::string>(oxbox::utilities::AsBytes(text),
                                     { .encoding = Encoding::UTF16, .order = std::endian::native }, { });
}
auto IdentityText(std::span<std::uint8_t const> utf8) -> std::string {
  using oxbox::utilities::Encoding;
  auto const text = std::string_view{ oxbox::utilities::SpanCast<char const>(utf8) };
  return TranscodeRange<std::string>(oxbox::utilities::AsBytes(text),
                                     { .encoding = Encoding::UTF8, .order = std::endian::native }, { });
}
template <class IdentityTy> auto NamesOf(IdentityTy const& identity) -> Identity {
  return { .user   = IdentityText(RecordArray<&IdentityTy::User, &IdentityTy::UserLength>(identity)),
           .domain = IdentityText(RecordArray<&IdentityTy::Domain, &IdentityTy::DomainLength>(identity)) };
}
auto Named(SEC_WINNT_AUTH_IDENTITY const& identity) -> Identity {
  if ((identity.Flags & SEC_WINNT_AUTH_IDENTITY_UNICODE) != 0) return NamesOf(identity);
  // FreeRDP on Linux fills ANSI identities from UTF-8 settings (3.32 winpr/libwinpr/sspi/sspi_winpr.c:621).
  // WinPR declares the W and A identities with one layout (sspi.h); Flags says which pointer type is live.
  return NamesOf(std::bit_cast<SEC_WINNT_AUTH_IDENTITY_A>(identity));
}

auto CauseOf(std::uint32_t code) -> Cause {
  switch (code) {
  case FREERDP_ERROR_SUCCESS:                          return Cause::None;
  case FREERDP_ERROR_CONNECT_TRANSPORT_FAILED:         return Cause::TransportFailed;
  case FREERDP_ERROR_AUTHENTICATION_FAILED:            return Cause::AuthenticationFailed;
  case FREERDP_ERROR_LOGOFF_BY_USER:                   return Cause::LogoffByUser;
  case FREERDP_ERROR_DISCONNECTED_BY_OTHER_CONNECTION: return Cause::OtherConnection;
  case FREERDP_ERROR_RPC_INITIATED_DISCONNECT:         return Cause::RpcInitiated;
  case FREERDP_ERROR_SERVER_DENIED_CONNECTION:         return Cause::ServerDenied;
  default:                                             return Cause::Other;
  }
}
auto ErrorInfo(Refusal refusal) -> std::uint32_t {
  switch (refusal) {
  case Refusal::ServerDenied:    return ERRINFO_SERVER_DENIED_CONNECTION;
  case Refusal::OtherConnection: return ERRINFO_DISCONNECTED_BY_OTHER_CONNECTION;
  default:                       Unreachable(refusal);
  }
}

constexpr OperationName PeerActivation      { "Peer activation"               };
constexpr OperationName PeerCapabilities    { "Peer capabilities"             };
constexpr OperationName PeerLogon           { "Peer logon"                    };
constexpr OperationName NtlmHashing         { "NTLM hash"                     };
constexpr OperationName FrameAcknowledgement{ "Surface frame acknowledgement" };
constexpr OperationName OutputSuppression   { "Suppress output"               };
constexpr OperationName KeyboardInput       { "Keyboard event"                };
constexpr OperationName UnicodeInput        { "Unicode keyboard event"        };
constexpr OperationName MouseInput          { "Mouse event"                   };
constexpr OperationName ExtendedMouseInput  { "Extended mouse event"          };
auto Events(freerdp_peer const& peer) -> ConnectionEvents& {
  return CallbackOwner<ConnectionEvents, &freerdp_peer::ContextExtra>(peer);
}
auto ContextEvents(rdpContext const& context) -> ConnectionEvents& {
  Expects(context.peer != nullptr, "the callback context has its peer");
  return Events(*context.peer);
}
auto Sink(rdpInput const& input) -> InputSink& {
  return CallbackOwner<InputSink, &rdpInput::param1>(input);
}

// NTLM keys are an MD5 digest wide (MS-NLMP 3.3.2).
using NtKey = std::span<std::uint8_t, 16>;
// The refusal is the authenticator's to count and log, outside any sink, so its own throw reaches the slot.
auto Refused(ConnectionEvents& events, std::optional<std::string> const& cause) -> std::int32_t {
  events.NtlmRefused(cause.value_or(""));
  return SEC_E_LOGON_DENIED;
}
// FreeRDP 3.32 ntlm_compute.c:513 passes the 16-byte hash buffer by its first byte, every other argument set.
auto ResponseKey(ConnectionEvents& events, SEC_WINNT_AUTH_IDENTITY const& identity, SecBuffer const& /*proof*/,
                 std::uint8_t const& /*random_key*/, std::uint8_t const& /*mic*/, SecBuffer const& /*mic_value*/,
                 NtKey response) -> std::int32_t {
  std::optional<std::string> cause;
  auto const                 noted = [&cause](std::string_view failure) { cause.emplace(failure); };
  auto const claimed = Contained(std::optional<Identity>{ }, [&] { return std::optional{ Named(identity) }; }, noted);
  if (!claimed) return Refused(events, cause);
  auto const hash = events.NtlmHash(*claimed);
  if (!hash) return SEC_E_LOGON_DENIED;
  // FreeRDP 3.32 ntlm_compute.c:513 takes the NTLMv2 response key, not the NT hash.
  auto const derived = [&] { return std::optional{ NtOwfV2(*hash, Utf16(claimed->user), Utf16(claimed->domain)) }; };
  auto const key     = Contained(std::optional<NtOwf>{ }, derived, noted);
  if (!key) return Refused(events, cause);
  std::ranges::copy(key->Bytes(), response.begin());
  return SEC_E_OK;
}
auto InstallPeerSlots(freerdp_peer& peer) -> void {
  constexpr auto logon = [](ConnectionEvents& events, SEC_WINNT_AUTH_IDENTITY const& /*identity*/, int automatic) {
    return events.Logon(automatic != 0);
  };
  // abi: psPeerActivate, psPeerCapabilities, psPeerPostConnect, psPeerLogon; BOOL is int
  peer.Activate     = Handled<Events, &ConnectionEvents::Activate, PeerActivation, SinkFailures, false>;
  peer.Capabilities = Handled<Events, &ConnectionEvents::Capabilities, PeerCapabilities, SinkFailures, false>;
  // PostConnect has no work that can fail: the session starts at Activate.
  peer.PostConnect = [](freerdp_peer*) noexcept -> int { return true; };
  peer.Logon       = Handled<Events, logon, PeerLogon, SinkFailures, false>;
  // abi: psSspiNtlmHashCallback, SECURITY_STATUS is LONG, an int32_t
  peer.SspiNtlmHashCallback = Handled<Events, ResponseKey, NtlmHashing, SinkFailures, SEC_E_INTERNAL_ERROR>;
}
auto InstallUpdateSlots(rdpUpdate& update) -> void {
  constexpr auto acknowledged = [](ConnectionEvents& events, std::uint32_t frame) {
    events.FrameAcknowledged(frame);
    return true;
  };
  constexpr auto suppressed   = [](ConnectionEvents& events, std::uint8_t allow) {
    events.SuppressOutput(allow != 0);
    return true;
  };
  // abi: pSurfaceFrameAcknowledge, pSuppressOutput; BOOL is int
  update.SurfaceFrameAcknowledge = Handled<ContextEvents, acknowledged, FrameAcknowledgement, SinkFailures, false>;
  update.SuppressOutput          = Handled<ContextEvents, suppressed, OutputSuppression, SinkFailures, false>;
}

// MS-RDPBCGR 2.2.8.1.1.3.1.1.3: a wheel rotation is 9-bit two's complement, 120 units a notch.
constexpr int                           WheelSignExtension = 0x200;
constexpr float                         WheelNotch         = 120.0F;
constexpr ButtonFlags<std::uint16_t, 3> MouseButtons       { { { PTR_FLAGS_BUTTON1, PointerButton::Left   },
                                                               { PTR_FLAGS_BUTTON2, PointerButton::Right  },
                                                               { PTR_FLAGS_BUTTON3, PointerButton::Middle } } };
constexpr ButtonFlags<std::uint16_t, 2> ExtendedButtons    { { { PTR_XFLAGS_BUTTON1, PointerButton::Back    },
                                                               { PTR_XFLAGS_BUTTON2, PointerButton::Forward } } };
auto Wheel(std::uint16_t flags) -> std::optional<WheelTurn> {
  if (!(flags & (PTR_FLAGS_WHEEL | PTR_FLAGS_HWHEEL))) return std::nullopt;
  int rotation = flags & WheelRotationMask;
  if (flags & PTR_FLAGS_WHEEL_NEGATIVE) rotation -= WheelSignExtension;
  float const notches = static_cast<float>(rotation) / WheelNotch;
  return WheelTurn{ .horizontal = (flags & PTR_FLAGS_HWHEEL) ? notches : 0,
                    .vertical   = (flags & PTR_FLAGS_WHEEL) ? notches : 0 };
}
auto MousePointer(std::uint16_t flags, std::uint16_t x, std::uint16_t y) -> PointerEvent {
  return { .buttons = Buttons(flags, MouseButtons),
           .down    = (flags & PTR_FLAGS_DOWN) != 0,
           .moved   = (flags & PTR_FLAGS_MOVE) != 0,
           .x       = x,
           .y       = y,
           .wheel   = Wheel(flags) };
}
auto ExtendedPointer(std::uint16_t flags, std::uint16_t x, std::uint16_t y) -> PointerEvent {
  return { .buttons = Buttons(flags, ExtendedButtons), .down = (flags & PTR_XFLAGS_DOWN) != 0, .x = x, .y = y };
}
auto InstallInputSlots(rdpInput& input) -> void {
  constexpr auto key      = [](InputSink& sink, std::uint16_t flags, std::uint8_t code) {
    sink.Key({ .code = code, .extended = (flags & KBD_FLAGS_EXTENDED) != 0, .down = !(flags & KBD_FLAGS_RELEASE) });
    return true;
  };
  constexpr auto unicode  = [](InputSink& sink, std::uint16_t flags, std::uint16_t code) {
    sink.Unicode({ .code = static_cast<char16_t>(code), .down = !(flags & KBD_FLAGS_RELEASE) });
    return true;
  };
  constexpr auto mouse    = [](InputSink& sink, std::uint16_t flags, std::uint16_t x, std::uint16_t y) {
    return sink.Pointer(MousePointer(flags, x, y));
  };
  constexpr auto extended = [](InputSink& sink, std::uint16_t flags, std::uint16_t x, std::uint16_t y) {
    return sink.Pointer(ExtendedPointer(flags, x, y));
  };
  // abi: pKeyboardEvent, pUnicodeKeyboardEvent, pMouseEvent, pExtendedMouseEvent; BOOL is int
  input.KeyboardEvent        = Handled<Sink, key, KeyboardInput, SinkFailures, false>;
  input.UnicodeKeyboardEvent = Handled<Sink, unicode, UnicodeInput, SinkFailures, false>;
  input.MouseEvent           = Handled<Sink, mouse, MouseInput, SinkFailures, false>;
  input.ExtendedMouseEvent   = Handled<Sink, extended, ExtendedMouseInput, SinkFailures, false>;
}
}
auto Unobserve(rdp_freerdp_peer* peer) noexcept -> void {
  peer->ContextExtra           = nullptr;
  peer->context->input->param1 = nullptr;
}
Connection::Connection(PeerHandle accepted)
    : _peer_socket{ SocketOf(accepted) }, _peer{ WithContext(std::move(accepted)) } { }
Connection::Connection(Socket socket) : Connection{ Adopted(std::move(socket)) } { }
auto Connection::Observe(ConnectionEvents& events, InputSink& input) -> Observation {
  auto& peer = Peer();
  Expects(peer.ContextExtra == nullptr, "a connection is observed once");
  peer.ContextExtra = &events;
  InstallPeerSlots(peer);
  InstallUpdateSlots(*peer.context->update);
  peer.context->input->param1 = &input;
  InstallInputSlots(*peer.context->input);
  return Observation{ &peer };
}
auto Connection::Settings() const noexcept -> SettingsReader {
  return SettingsReader{ *Peer().context->settings };
}
auto Connection::Settings() noexcept -> SettingsView {
  return SettingsView{ *Peer().context->settings };
}
auto Connection::Initialize() -> bool {
  auto& peer = Peer();
  return peer.Initialize(&peer);
}
auto Connection::Pump() -> bool {
  auto& peer = Peer();
  return peer.CheckFileDescriptor(&peer);
}
auto Connection::EventHandles(std::span<WaitHandle> budget) const -> std::span<WaitHandle> {
  return WaitHandle::Collected<&freerdp_peer::GetEventHandles>(Peer(), budget);
}
auto Connection::Active() const -> bool {
  return freerdp_is_active_state(Peer().context);
}
auto Connection::WriteBlocked() const -> bool {
  auto& peer = Peer();
  return peer.IsWriteBlocked(&peer);
}
auto Connection::DrainOutput() -> bool {
  auto& peer = Peer();
  return peer.DrainOutputBuffer(&peer) >= 0;
}
auto Connection::PeerSocket() const noexcept -> NativeSocket {
  return _peer_socket;
}
auto Connection::Hostname() const noexcept -> std::string_view {
  return Peer().hostname;
}
auto Connection::Claimed() const -> Identity {
  return Named(Peer().identity);
}
auto Connection::Authenticated() const noexcept -> bool {
  return Peer().authenticated != 0;
}
auto Connection::Identify(Identity const& identity) -> void {
  if (sspi_SetAuthIdentityA(&Peer().identity, identity.user.c_str(), identity.domain.c_str(), nullptr) <= 0)
    throw AllocationFailed{ "Client identity" };
}
auto Connection::SetAuthenticated(bool authenticated) noexcept -> void {
  Peer().authenticated = authenticated;
}
auto Connection::OfferReconnect(std::uint32_t logon_id) -> bool {
  ReconnectCookie cookie{ .logon_id = logon_id };
  if (winpr_RAND(cookie.random_bits.data(), cookie.random_bits.size()) != 0) return false;
  Settings().SetAutoReconnectCookie(cookie);
  logon_info_ex info{ };
  info.haveCookie = true;
  info.LogonId    = cookie.logon_id;
  std::ranges::copy(cookie.random_bits, info.ArcRandomBits);
  auto& context = Context();
  return context.update->SaveSessionInfo(&context, INFO_TYPE_LOGON_EXTENDED_INF, &info);
}
auto Connection::AcceptTls() -> bool {
  auto const* const io = freerdp_get_io_callbacks(&Context());
  Expects(io != nullptr, "the peer has transport callbacks");
  Expects(io->TLSAccept != nullptr, "the peer can accept TLS");
  return io->TLSAccept(freerdp_get_transport(&Context())) != 0;
}
auto Connection::Error() const -> LastError {
  auto const code = freerdp_get_last_error(Peer().context);
  return { .cause = CauseOf(code), .name = freerdp_get_last_error_name(code) };
}
auto Connection::Refuse(Refusal refusal) -> void {
  auto const& context = Context();
  freerdp_set_error_info(context.rdp, ErrorInfo(refusal));
  freerdp_send_error_info(context.rdp);
}
auto Connection::Disconnect() noexcept -> void {
  auto& peer = Peer();
  peer.Disconnect(&peer);
}
auto Connection::Close() -> void {
  auto& peer = Peer();
  peer.Close(&peer);
}
auto Connection::Context() noexcept -> rdp_context& {
  return *Peer().context;
}
auto Connection::Peer() const noexcept -> rdp_freerdp_peer& {
  Expects(_peer != nullptr, "the connection is not moved from");
  return *_peer;
}
}
