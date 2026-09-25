#pragma once
#include <freerdp/channels/rdpgfx.h>
#include <chrono>

namespace sdl_rdp::video::detail::graphics_timing {
class GraphicsTiming {
public:
  auto Ready(std::chrono::nanoseconds elapsed) noexcept               -> void;
  auto Record(RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const& value) noexcept -> void;
  auto ReadyTime() const noexcept                                     -> std::chrono::nanoseconds;
  auto Qoe() const noexcept                                           -> RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const&;

private:
  std::chrono::nanoseconds         _ready_time{ };
  RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU _qoe       { };
};
}

namespace sdl_rdp::video {
using detail::graphics_timing::GraphicsTiming;
}
