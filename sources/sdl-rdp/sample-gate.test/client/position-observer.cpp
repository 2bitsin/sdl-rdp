#include <sdl-rdp/sample-gate.test/client/position-observer.hpp>
#include <cstddef>
#include <cstdint>

namespace sdl_rdp::sample_gate_test::client::detail::position_observer {
using sdl_rdp::utilities::Expects;

PositionObserver::PositionObserver(Client& client) {
  Expects(!active, "one pointer observer per thread");
  active = this;
  // abi: pPointerPosition, BOOL is int
  client.Instance()->context->update->pointer->PointerPosition = [](rdpContext*,
                                                                    POINTER_POSITION_UPDATE const* position) -> int {
    Expects(active, "observer is installed");
    Expects(position, "position observer exists");
    active->Receive(*position);
    return true;
  };
}
PositionObserver::~PositionObserver() {
  active = nullptr;
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
