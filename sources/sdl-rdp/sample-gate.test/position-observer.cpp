#include <sdl-rdp/sample-gate.test/position-observer.hpp>

namespace SampleGate {
using utilities::Expects;

PositionObserver::PositionObserver(Headless::Client& client) {
  Expects(!active, "one pointer observer per thread");
  active                                                       = this;
  client.Instance()->context->update->pointer->PointerPosition = Receive;
}
PositionObserver::~PositionObserver() {
  active = nullptr;
}
auto PositionObserver::Count() const -> unsigned {
  return count;
}
auto PositionObserver::X() const -> unsigned {
  return x;
}
auto PositionObserver::Y() const -> unsigned {
  return y;
}
auto PositionObserver::Receive(rdpContext* /*unused*/, POINTER_POSITION_UPDATE const* position) -> BOOL {
  Expects(active, "observer is installed");
  Expects(position, "position observer exists");
  ++active->count;
  active->x = position->xPos;
  active->y = position->yPos;
  return TRUE;
}
}
