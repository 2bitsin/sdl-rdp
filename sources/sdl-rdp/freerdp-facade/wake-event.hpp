#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>

#include <atomic>

namespace sdl_rdp::freerdp_facade::detail::wake_event {
class WakeEvent {
public:
  enum class Phase{ Idle, Pending };
  explicit WakeEvent(EventHandle value);
  auto     Handle() const         -> WaitHandle;
  auto     Transition(Phase next) -> void;

private:
  EventHandle        handle;
  std::atomic<Phase> phase { Phase::Idle };
};
}

namespace sdl_rdp::freerdp_facade {
using detail::wake_event::WakeEvent;
}
