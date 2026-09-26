#include <sdl-rdp/sample-gate.test/client/position-observer.hpp>

#include <sdl-rdp/headless-client.test/client/handles.hpp>
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/sample-gate.test/client/pointer-updates.hpp>

#include <cstddef>
#include <cstdint>

namespace sdl_rdp::sample_gate_test::client::detail::position_observer {
using sdl_rdp::headless_client_test::client::ClientContext;
using sdl_rdp::headless_client_test::utilities::ObserverSet;
using sdl_rdp::utilities::Expects;

PositionObserver::PositionObserver(Client& client)
    : context(ClientContext(client)), pointer(PointerUpdates(client)), original(pointer.PointerPosition),
      membership(context, *this) {
  // abi: pPointerPosition, BOOL is int
  pointer.PointerPosition = [](rdpContext* context, POINTER_POSITION_UPDATE const* position) -> int {
    Expects(context != nullptr, "the pointer position names its client context");
    Expects(position != nullptr, "the pointer position is supplied");
    ObserverSet::Of(*context).Held<PositionObserver>()->Receive(*position);
    return true;
  };
}
PositionObserver::~PositionObserver() {
  pointer.PointerPosition = original;
}
auto PositionObserver::Count() const -> std::size_t {
  return count;
}
auto PositionObserver::X() const -> std::uint32_t {
  return x;
}
auto PositionObserver::Y() const -> std::uint32_t {
  return y;
}
auto PositionObserver::Receive(POINTER_POSITION_UPDATE const& position) -> void {
  ++count;
  x = position.xPos;
  y = position.yPos;
}
}
