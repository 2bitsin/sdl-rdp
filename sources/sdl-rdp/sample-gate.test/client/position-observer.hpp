#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <cstddef>
#include <cstdint>

namespace sdl_rdp::sample_gate_test::client::detail::position_observer {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::utilities::Membership;
using sdl_rdp::utilities::Pinned;

class PositionObserver : private Pinned {
public:
  explicit PositionObserver(Client& client);
           ~PositionObserver();
  auto     Count() const -> std::size_t;
  auto     X() const     -> std::uint32_t;
  auto     Y() const     -> std::uint32_t;

private:
  auto Receive(POINTER_POSITION_UPDATE const& position) -> void;
  rdpContext&                  context;
  rdpPointerUpdate&            pointer;
  pPointerPosition             original;
  std::size_t                  count      = 0;
  std::uint32_t                x          = 0;
  std::uint32_t                y          = 0;
  Membership<PositionObserver> membership;
};
}

namespace sdl_rdp::sample_gate_test::client {
using detail::position_observer::PositionObserver;
}
