#include "_detail/output-control.hpp"

#include "_detail/activation.hpp"
#include "_detail/frame-pacing.hpp"
#include "_detail/graphics-link.hpp"
#include "_detail/peer-frames.hpp"
#include "_detail/peer-link.hpp"

namespace Backend {
OutputControl::OutputControl(PeerLink& link, GraphicsLink const& graphics, FramePacing& pacing,
                             Activation& activation, PeerFrames& frames) noexcept
    : _link { link }, _graphics{ graphics }, _pacing{ pacing }, _activation{ activation }, _frames{ frames } { }
void OutputControl::Acknowledge(UINT32 id) {
  if (!_graphics.Confirmed()) _pacing.Accept(id);
}
void OutputControl::Suppress(bool allow) {
  if (!allow) {
    _activation.Suppress();
    return;
  }
  _activation.Resume();
  _frames.Refresh();
  _link.Signal();
}
}
