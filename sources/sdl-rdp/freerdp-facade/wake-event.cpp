#include <sdl-rdp/freerdp-facade/wake-event.hpp>

#include <sdl-rdp/freerdp-facade/exceptions.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <winpr/file.h>
#include <winpr/synch.h>
#include <utility>

namespace Backend {
WakeEvent::WakeEvent(EventHandle value) : handle(std::move(value)) {
  utilities::Expects(handle != nullptr, "wake event owns an event");
}
auto WakeEvent::get() const -> HANDLE {
  return handle.get();
}
auto WakeEvent::Transition(Phase next) -> void {
  switch (next) {
  case Phase::Pending:
    if (phase.exchange(next) == Phase::Idle) SetEvent(get());
    break;
  case Phase::Idle:
    ResetEvent(get());
    phase.store(next);
    break;
  default: utilities::Unreachable("known wake phase");
  }
}
}
