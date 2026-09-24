#include <sdl-rdp/video/graphics-timing.hpp>

namespace Backend {
auto GraphicsTiming::Ready(std::chrono::nanoseconds elapsed) noexcept -> void {
  _ready_time = elapsed;
}
auto GraphicsTiming::Record(RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const& value) noexcept -> void {
  _qoe = value;
}
auto GraphicsTiming::ReadyTime() const noexcept -> std::chrono::nanoseconds {
  return _ready_time;
}
auto GraphicsTiming::Qoe() const noexcept -> RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const& {
  return _qoe;
}
}
