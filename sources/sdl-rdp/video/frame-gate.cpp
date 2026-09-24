#include <sdl-rdp/video/frame-gate.hpp>

#include <sdl-rdp/core/activation.hpp>
#include <sdl-rdp/core/desktop-layout.hpp>
#include <sdl-rdp/core/peer-link.hpp>
#include <sdl-rdp/video/frame-pacing.hpp>
#include <sdl-rdp/video/graphics-link.hpp>
#include <sdl-rdp/video/peer-frames.hpp>

#include <freerdp/freerdp.h>

namespace Backend {
FrameGate::FrameGate(PeerLink& link, FrameStore& store, PeerFrames& frames, DesktopLayout& desktop, FramePacing& pacing,
                     Activation const& activation, GraphicsLink const& graphics) noexcept
    : _link{ link }, _store{ store }, _frames{ frames }, _desktop{ desktop }, _pacing{ pacing },
      _activation{ activation }, _graphics{ graphics } { }
auto FrameGate::Settle() -> bool {
  auto const frame = _store.Lock();
  _link.Settle();
  if (_link.WriteBlocked()) {
    _pacing.Blocked();
    return false;
  }
  if (!freerdp_is_active_state(&_link.Context())) return false;
  if (_desktop.EndResize()) {
    _pacing.Restart(frame);
    _frames.Invalidate(frame);
  }
  _pacing.Drained();
  return true;
}
auto FrameGate::Admit() -> bool {
  return Settle() && _pacing.Admit([this] { return _graphics.Capacity(); }) && !_activation.Suppressed();
}
}
