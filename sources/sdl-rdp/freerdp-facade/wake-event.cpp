#include <sdl-rdp/freerdp-facade/wake-event.hpp>

#include <sdl-rdp/freerdp-facade/exceptions.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <winpr/file.h>
#include <winpr/synch.h>
#include <utility>

namespace sdl_rdp::freerdp_facade::detail::wake_event {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Unreachable;

WakeEvent::WakeEvent(EventHandle value) : handle(std::move(value)) {
  Expects(handle != nullptr, "wake event owns an event");
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
  default: Unreachable("known wake phase");
  }
}
}
