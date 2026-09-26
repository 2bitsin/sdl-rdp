#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

namespace sdl_rdp::sample_gate_test::frame::detail::first_size {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::utilities::Membership;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Pinned;

class FirstFrameSize : private Pinned {
public:
  explicit FirstFrameSize(Client& value);
           ~FirstFrameSize();
  auto     Received() const -> bool;
  auto     Size() const     -> Extent;

private:
  auto Connect(freerdp& instance) -> bool;
  auto Paint(rdpContext& context) -> bool;
  bool   received = false;
  Extent size;

  Client&                        client;
  decltype(freerdp::PostConnect) original_connect;
  pEndPaint                      original_paint   = nullptr;
  bool                           paint_installed  = false;
  Membership<FirstFrameSize>     membership;
};
}

namespace sdl_rdp::sample_gate_test::frame {
using detail::first_size::FirstFrameSize;
}
