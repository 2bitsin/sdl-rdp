#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

namespace sdl_rdp::sample_gate_test::client::detail::pointer_observer {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::utilities::Membership;
using sdl_rdp::utilities::Pinned;

class PointerObserver : private Pinned {
public:
  explicit PointerObserver(Client& client);
           ~PointerObserver();
  auto     Red() const -> bool;

private:
  auto Receive(POINTER_NEW_UPDATE const& update) -> void;
  rdpContext&                 context;
  rdpPointerUpdate&           pointer;
  pPointerNew                 original;
  bool                        red        = false;
  Membership<PointerObserver> membership;
};
}

namespace sdl_rdp::sample_gate_test::client {
using detail::pointer_observer::PointerObserver;
}
