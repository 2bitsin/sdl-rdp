#include "_detail/wake-event.hpp"
#include "_detail/contract.hpp"
#include <winpr/synch.h>

namespace Backend {
WakeEvent::WakeEvent(HANDLE value) : handle(value) {}
HANDLE WakeEvent::get() const { return handle.get(); }
WakeEvent::operator bool() const { return bool(handle); }
void WakeEvent::Transition(Phase next)
{
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
