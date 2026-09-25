#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>

namespace sdl_rdp::sample_gate_test::frame::detail::first_size {
using sdl_rdp::headless_client_test::client::Client;

class FirstFrameSize {
public:
           FirstFrameSize(FirstFrameSize const&)               = delete;
           FirstFrameSize(FirstFrameSize&&)                    = delete;
  explicit FirstFrameSize(Client& value);
           ~FirstFrameSize();
  auto     operator=(FirstFrameSize const&) -> FirstFrameSize& = delete;
  auto     operator=(FirstFrameSize&&)      -> FirstFrameSize& = delete;
  auto     Received() const                 -> bool;
  auto     Width() const                    -> int;
  auto     Height() const                   -> int;

private:
  auto Connect(freerdp& instance) -> bool;
  auto Paint(rdpContext& context) -> bool;
  bool received = false;
  int  width    = 0;
  int  height   = 0;

  inline static thread_local FirstFrameSize* active           = nullptr;
  Client&                                    client;
  decltype(freerdp::PostConnect)             original_connect;
  pEndPaint                                  original_paint   = nullptr;
  bool                                       paint_installed  = false;
};
}

namespace sdl_rdp::sample_gate_test::frame {
using detail::first_size::FirstFrameSize;
}
