#include "_detail/arrival.hpp"

#include "_detail/activation.hpp"
#include "_detail/auth.hpp"
#include "_detail/desktop-layout.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/extent.hpp"
#include "_detail/frame-pacing.hpp"
#include "_detail/peer-frames.hpp"
#include "_detail/peer-link.hpp"
#include "_detail/session-access.hpp"

#include <freerdp/settings.h>
#include <cstring>
#include <format>

namespace Backend {
namespace {
auto Connected(rdpSettings const& settings) -> sdlrdp_event {
  sdlrdp_event event{ .type = SDLRDP_CONNECTED };
  event.connected.width           = freerdp_settings_get_uint32(&settings, FreeRDP_DesktopWidth);
  event.connected.height          = freerdp_settings_get_uint32(&settings, FreeRDP_DesktopHeight);
  event.connected.keyboard_layout = freerdp_settings_get_uint32(&settings, FreeRDP_KeyboardLayout);
  event.connected.bpp             = freerdp_settings_get_uint32(&settings, FreeRDP_ColorDepth);
  auto const* name = freerdp_settings_get_string(&settings, FreeRDP_ClientHostname);
  if (name) std::strncpy(event.connected.client_name, name, sizeof(event.connected.client_name) - 1);
  return event;
}
auto Acknowledging(rdpSettings const& settings) -> AcknowledgementMode {
  return freerdp_settings_get_uint32(&settings, FreeRDP_FrameAcknowledge) ? AcknowledgementMode::Tracking
                                                                          : AcknowledgementMode::Suspended;
}
}
Arrival::Arrival(SessionAccess& session, FrameStore& store, PeerFrames& frames, PeerLink& link, Activation& activation,
                 DesktopLayout& desktop, FramePacing& pacing, Diagnostics const& diagnostics) noexcept
    : _session{ session }, _store{ store }, _frames{ frames }, _link{ link }, _activation{ activation },
      _desktop{ desktop }, _pacing{ pacing }, _diagnostics{ diagnostics } { }
auto Arrival::Connection(sdlrdp_codec codec) const -> sdlrdp_event {
  auto const screen = _desktop.ScreenEvent().screen;
  auto       event  = Connected(_link.Settings());
  AuthenticationIdentity(_link.Client(), event);
  event.connected.refresh_millihertz = _pacing.Effective() * MillihertzPerHz;
  event.connected.codec              = codec;
  event.connected.screen_width       = screen.width;
  event.connected.screen_height      = screen.height;
  return event;
}
auto Arrival::Enter(sdlrdp_event const& connection, sdlrdp_codec codec) -> void {
  auto const frame = _session.Takeover(_link);
  _activation.Activate();
  if (auto const& shadow = _store.Snapshot(frame)) {
    _frames.Post(frame, shadow.Bounds());
    _link.Signal();
  }
  _activation.Hold(connection, _desktop.ScreenEvent());
  if (!freerdp_settings_get_bool(&_link.Settings(), FreeRDP_SupportGraphicsPipeline))
    _activation.Announce(codec, _pacing.Effective());
}
auto Arrival::Admit(sdlrdp_codec codec) -> void {
  auto const connection = Connection(codec);
  _pacing.Acknowledgements(Acknowledging(_link.Settings()));
  _desktop.Assign(Whole({ .width = connection.connected.width, .height = connection.connected.height }));
  Enter(connection, codec);
  _store.Notify();
  _session.AudioChanged();
  _diagnostics.Line("connect", [&] { return std::format("client={}", connection.connected.client_name); });
}
}
