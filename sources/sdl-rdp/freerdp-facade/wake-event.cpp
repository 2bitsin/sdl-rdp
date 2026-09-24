#include <sdl-rdp/freerdp-facade/wake-event.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <winpr/file.h>
#include <winpr/synch.h>
#include <stdexcept>

namespace Backend {
auto Signalled(HANDLE event) -> bool {
  utilities::Expects(event != nullptr, "event exists");
  utilities::Expects(event != INVALID_HANDLE_VALUE, "event handle is valid");
  auto result = WaitForSingleObject(event, 0);
  if (result == WAIT_FAILED) throw std::runtime_error("Event readiness wait failed.");
  return result == WAIT_OBJECT_0;
}

WakeEvent::WakeEvent(HANDLE value) : handle(value) { }
auto WakeEvent::get() const -> HANDLE {
  return handle.get();
}
WakeEvent::operator bool() const {
  return bool(handle);
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
