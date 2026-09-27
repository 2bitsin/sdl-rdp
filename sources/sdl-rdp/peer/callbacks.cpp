#include <sdl-rdp/peer/peer.hpp>

#include <sdl-rdp/auth/authenticator.hpp>
#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/peer/arrival.hpp>
#include <sdl-rdp/picture/desktop-layout.hpp>
#include <sdl-rdp/picture/frame-store.hpp>
#include <sdl-rdp/video/encoder.hpp>
#include <sdl-rdp/video/frame/pacing.hpp>
#include <sdl-rdp/video/output-control.hpp>

#include <algorithm>
#include <array>
#include <cstdint>

namespace sdl_rdp::peer::detail::peer {
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::picture::ApplyDesktopSize;

namespace {
using sdl_rdp::freerdp_facade::NumberKey;
constexpr std::uint32_t                SessionLogonId = 1;
constexpr std::array<std::uint32_t, 3> ColourDepths   { 16, 24, 32 };
}
auto Peer::Activate() -> bool {
  if (_activation.Active()) {
    _link.Signal();
    return true;
  }
  auto& connection = _link.Connection();
  if (!_authenticator.VerifySettings() || !connection.OfferReconnect(SessionLogonId)) return false;
  _encoder.Select(connection.Settings(), _configuration.CodecPreference());
  _arrival.Admit(_encoder.SelectedCodec());
  return true;
}
auto Peer::Capabilities() -> bool {
  if (!_authenticator.VerifySettings()) return false;
  auto const settings = _link.Connection().Settings();
  auto const frame    = _store.Lock();
  if (!_activation.Activated()) {
    _pacing.Restart(frame);
    _desktop.RecordScreen(settings);
  }
  if (!std::ranges::contains(ColourDepths, settings.Get(NumberKey::ColorDepth))) {
    _diagnostics.Log(LogLevel::Warn, "Connection refused: colour depth must be 16, 24 or 32 bpp.");
    return false;
  }
  ApplyDesktopSize(settings, _desktop.Offer(_store.Picture(frame)));
  return true;
}
auto Peer::Logon(bool automatic) -> bool {
  return _authenticator.Logon(automatic);
}
auto Peer::NtlmHash(Identity const& identity) -> std::optional<NtOwf> {
  return _authenticator.NtlmHash(identity);
}
auto Peer::NtlmRefused(std::string_view cause) -> void {
  _authenticator.NtlmRefused(cause);
}
auto Peer::FrameAcknowledged(std::uint32_t frame) -> void {
  _output.Acknowledge(frame);
}
auto Peer::SuppressOutput(bool allow) -> void {
  _output.Suppress(allow);
}
}
