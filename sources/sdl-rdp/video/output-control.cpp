#include <sdl-rdp/video/output-control.hpp>

#include <sdl-rdp/core/activation.hpp>
#include <sdl-rdp/core/peer-link.hpp>
#include <sdl-rdp/video/frame-pacing.hpp>
#include <sdl-rdp/video/graphics-link.hpp>
#include <sdl-rdp/video/peer-frames.hpp>
#include <cstdint>

namespace Backend {
OutputControl::OutputControl(PeerLink& link, GraphicsLink const& graphics, FramePacing& pacing, Activation& activation,
                             PeerFrames& frames) noexcept
    : _link{ link }, _graphics{ graphics }, _pacing{ pacing }, _activation{ activation }, _frames{ frames } { }
auto OutputControl::Acknowledge(std::uint32_t id) -> void {
  if (!_graphics.Confirmed()) _pacing.Accept(id);
}
auto OutputControl::Suppress(bool allow) -> void {
  if (!allow) {
    _activation.Suppress();
    return;
  }
  _activation.Resume();
  _frames.Refresh();
  _link.Signal();
}
}
