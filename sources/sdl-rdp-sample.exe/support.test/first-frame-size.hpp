#pragma once
#include <sdl-rdp-backend.so/_detail/client.hpp>

namespace SampleGate {
class FirstFrameSize {
public:
           FirstFrameSize(FirstFrameSize const&)                 = delete;
           FirstFrameSize(FirstFrameSize&&)                      = delete;
  explicit FirstFrameSize(Headless::Client& value);
           ~FirstFrameSize();
  auto     operator = (FirstFrameSize const&) -> FirstFrameSize& = delete;
  auto     operator = (FirstFrameSize&&)      -> FirstFrameSize& = delete;
  auto     Received() const                   -> bool;
  auto     Width() const                      -> int;
  auto     Height() const                     -> int;

private:
  static auto Connect(freerdp* instance) -> BOOL;
  static auto Paint(rdpContext* context) -> BOOL;
  bool received = false;
  int  width    = 0;
  int  height   = 0;

  inline static thread_local FirstFrameSize* active           = nullptr;
  Headless::Client&                          client;
  decltype(freerdp::PostConnect)             original_connect;
  pEndPaint                                  original_paint   = nullptr;
  bool                                       paint_installed  = false;
};
}
