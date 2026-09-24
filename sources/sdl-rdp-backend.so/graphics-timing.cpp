#include "_detail/graphics-timing.hpp"

namespace Backend {
void GraphicsTiming::Ready(std::chrono::nanoseconds elapsed) noexcept {
  _ready_time = elapsed;
}
void GraphicsTiming::Record(RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const& value) noexcept {
  _qoe = value;
}
std::chrono::nanoseconds GraphicsTiming::ReadyTime() const noexcept {
  return _ready_time;
}
RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const& GraphicsTiming::Qoe() const noexcept {
  return _qoe;
}
}
