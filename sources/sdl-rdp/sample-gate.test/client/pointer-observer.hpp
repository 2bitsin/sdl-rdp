#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>

namespace sdl_rdp::sample_gate_test::client::detail::pointer_observer {
using sdl_rdp::headless_client_test::client::Client;

struct PointerObserver {
public:
  explicit PointerObserver(Client& client);
  auto     Red() const -> bool;

private:
  auto Receive(POINTER_NEW_UPDATE const& update) -> void;
  inline static thread_local PointerObserver* active = nullptr;
  bool                                        red    = false;
};
}

namespace sdl_rdp::sample_gate_test::client {
using detail::pointer_observer::PointerObserver;
}
