#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>

namespace sdl_rdp::sample_gate_test::client::detail::pointer_observer {
using sdl_rdp::headless_client_test::client::Client;

class PointerObserver {
public:
           PointerObserver(PointerObserver const&)               = delete;
           PointerObserver(PointerObserver&&)                    = delete;
  explicit PointerObserver(Client& client);
           ~PointerObserver();
  auto     operator=(PointerObserver const&) -> PointerObserver& = delete;
  auto     operator=(PointerObserver&&)      -> PointerObserver& = delete;
  auto     Red() const                       -> bool;

private:
  auto Receive(POINTER_NEW_UPDATE const& update) -> void;
  rdpContext&       context;
  rdpPointerUpdate& pointer;
  pPointerNew       original;
  bool              red      = false;
};
}

namespace sdl_rdp::sample_gate_test::client {
using detail::pointer_observer::PointerObserver;
}
