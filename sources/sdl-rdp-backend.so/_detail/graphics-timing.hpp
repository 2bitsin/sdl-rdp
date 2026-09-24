#pragma once
#include <chrono>
#include <freerdp/channels/rdpgfx.h>

namespace Backend {
class GraphicsTiming {
public:
  void                                    Ready(std::chrono::nanoseconds elapsed)               noexcept;
  void                                    Record(RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const& value) noexcept;
  std::chrono::nanoseconds                ReadyTime() const                                     noexcept;
  RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const& Qoe() const                                           noexcept;

private:
  std::chrono::nanoseconds         _ready_time{ };
  RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU _qoe       { };
};
}
