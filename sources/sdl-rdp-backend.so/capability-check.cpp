#include "_detail/capability-check.hpp"

#include "_detail/activation.hpp"
#include "_detail/auth.hpp"
#include "_detail/desktop-layout.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/frame-pacing.hpp"
#include "_detail/peer-link.hpp"

#include <algorithm>
#include <array>
#include <freerdp/settings.h>

namespace Backend {
namespace {
constexpr std::array<UINT32, 3> ColourDepths{ 16, 24, 32 };
}
CapabilityCheck::CapabilityCheck(PeerLink& link, Authenticator& authenticator, Activation const& activation,
                                 FramePacing& pacing, DesktopLayout& desktop, FrameStore& store,
                                 Diagnostics const& diagnostics) noexcept
    : _link { link }, _authenticator{ authenticator }, _activation{ activation }, _pacing{ pacing },
      _desktop{ desktop }, _store{ store }, _diagnostics{ diagnostics } { }
BOOL CapabilityCheck::Accept() {
  if (!_authenticator.VerifySettings()) return FALSE;
  auto&      settings = _link.Settings();
  auto const frame    = _store.Lock();
  if (!_activation.Activated()) {
    _pacing.Restart(frame);
    _desktop.RecordScreen(settings);
  }
  if (!std::ranges::contains(ColourDepths, freerdp_settings_get_uint32(&settings, FreeRDP_ColorDepth))) {
    _diagnostics.Log(SDLRDP_LOG_WARN, "Connection refused: colour depth must be 16, 24 or 32 bpp.");
    return FALSE;
  }
  return ApplyDesktopSize(settings, _desktop.Offer(_store.Picture(frame)));
}
}
