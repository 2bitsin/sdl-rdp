#include <sdl-rdp/session/capability-check.hpp>

#include <sdl-rdp/auth/auth.hpp>
#include <sdl-rdp/core/activation.hpp>
#include <sdl-rdp/core/desktop-layout.hpp>
#include <sdl-rdp/core/diagnostics.hpp>
#include <sdl-rdp/core/peer-link.hpp>
#include <sdl-rdp/video/frame-pacing.hpp>

#include <freerdp/settings.h>
#include <algorithm>
#include <array>
#include <cstdint>

namespace Backend {
namespace {
constexpr std::array<std::uint32_t, 3> ColourDepths{ 16, 24, 32 };
}
CapabilityCheck::CapabilityCheck(PeerLink& link, Authenticator& authenticator, Activation const& activation,
                                 FramePacing& pacing, DesktopLayout& desktop, FrameStore& store,
                                 Diagnostics const& diagnostics) noexcept
    : _link{ link }, _authenticator{ authenticator }, _activation{ activation }, _pacing{ pacing }, _desktop{ desktop },
      _store{ store }, _diagnostics{ diagnostics } { }
auto CapabilityCheck::Accept() -> bool {
  if (!_authenticator.VerifySettings()) return false;
  auto&      settings = _link.Settings();
  auto const frame    = _store.Lock();
  if (!_activation.Activated()) {
    _pacing.Restart(frame);
    _desktop.RecordScreen(settings);
  }
  if (!std::ranges::contains(ColourDepths, freerdp_settings_get_uint32(&settings, FreeRDP_ColorDepth))) {
    _diagnostics.Log(SDLRDP_LOG_WARN, "Connection refused: colour depth must be 16, 24 or 32 bpp.");
    return false;
  }
  return ApplyDesktopSize(settings, _desktop.Offer(_store.Picture(frame)));
}
}
