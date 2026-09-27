#include <sdl-rdp/freerdp-facade/wake-event.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <utility>

namespace sdl_rdp::freerdp_facade::detail::wake_event {
using sdl_rdp::utilities::Unreachable;

WakeEvent::WakeEvent(EventHandle value) : handle(std::move(value)) { }
auto WakeEvent::Handle() const -> WaitHandle {
  return WaitHandle{ handle };
}
auto WakeEvent::Transition(Phase next) -> void {
  switch (next) {
  case Phase::Pending:
    if (phase.exchange(next) == Phase::Idle) handle.Set();
    break;
  case Phase::Idle:
    handle.Reset();
    phase.store(next);
    break;
  default: Unreachable("known wake phase");
  }
}
}
