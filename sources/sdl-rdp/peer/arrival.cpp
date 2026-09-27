#include <sdl-rdp/peer/arrival.hpp>

#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/link/session-access.hpp>
#include <sdl-rdp/picture/desktop-layout.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/video/frame/pacing.hpp>
#include <sdl-rdp/video/peer-frames.hpp>

#include <cstdint>
#include <format>
#include <utility>

namespace sdl_rdp::peer::detail::arrival {
using sdl_rdp::configuration::MillihertzPerHz;
using sdl_rdp::freerdp_facade::BoolKey;
using sdl_rdp::freerdp_facade::NumberKey;
using sdl_rdp::freerdp_facade::SettingsReader;
using sdl_rdp::link::ClientHostname;
using sdl_rdp::link::ScreenChanged;
using sdl_rdp::utilities::Whole;
using sdl_rdp::video::frame::AcknowledgementMode;

namespace {
auto ConnectedFrom(PeerLink const& link, DesktopLayout const& desktop, std::uint32_t refresh_millihertz, Codec codec)
    -> Connected {
  auto const& connection = link.Connection();
  auto const  settings   = connection.Settings();
  auto const  screen     = desktop.Screen();
  auto        claim      = connection.Claimed();
  return { .width              = settings.Get(NumberKey::DesktopWidth),
           .height             = settings.Get(NumberKey::DesktopHeight),
           .bpp                = settings.Get(NumberKey::ColorDepth),
           .client_name        = ClientHostname(link),
           .codec              = codec,
           .screen_width       = screen.width,
           .screen_height      = screen.height,
           .refresh_millihertz = refresh_millihertz,
           .keyboard_layout    = settings.Get(NumberKey::KeyboardLayout),
           .user               = std::move(claim.user),
           .domain             = std::move(claim.domain),
           .authenticated      = connection.Authenticated() };
}
auto ScreenOf(DesktopLayout const& desktop) -> ScreenChanged {
  auto const screen = desktop.Screen();
  return { .width = screen.width, .height = screen.height };
}
auto Acknowledging(SettingsReader settings) -> AcknowledgementMode {
  return settings.Get(NumberKey::FrameAcknowledge) ? AcknowledgementMode::Tracking : AcknowledgementMode::Suspended;
}
}
Arrival::Arrival(SessionAccess& session, FrameStore& store, PeerFrames& frames, PeerLink& link, Activation& activation,
                 DesktopLayout& desktop, FramePacing& pacing, Diagnostics const& diagnostics) noexcept
    : _session{ session }, _store{ store }, _frames{ frames }, _link{ link }, _activation{ activation },
      _desktop{ desktop }, _pacing{ pacing }, _diagnostics{ diagnostics } { }
auto Arrival::Connection(Codec codec) const -> Connected {
  return ConnectedFrom(_link, _desktop, _pacing.Effective() * MillihertzPerHz, codec);
}
auto Arrival::Enter(Connected connection, Codec codec) -> void {
  auto const frame = _session.Takeover(_link);
  _activation.Activate();
  if (auto const& shadow = _store.Snapshot(frame)) {
    _frames.Post(frame, shadow.Bounds());
    _link.Signal();
  }
  _activation.Hold(std::move(connection), ScreenOf(_desktop));
  if (!_link.Connection().Settings().Get(BoolKey::SupportGraphicsPipeline))
    _activation.Announce(codec, _pacing.Effective());
}
auto Arrival::Admit(Codec codec) -> void {
  auto connection = Connection(codec);
  _pacing.Acknowledgements(Acknowledging(_link.Connection().Settings()));
  _desktop.Assign(Whole({ .width = connection.width, .height = connection.height }));
  _diagnostics.Line("connect", [&] { return std::format("client={}", connection.client_name); });
  Enter(std::move(connection), codec);
  _store.Notify();
  _session.AudioChanged();
}
}
