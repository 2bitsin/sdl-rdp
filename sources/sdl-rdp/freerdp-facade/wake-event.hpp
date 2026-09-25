#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>

#include <atomic>

namespace sdl_rdp::freerdp_facade::detail::wake_event {
class WakeEvent {
public:
  enum class Phase{ Idle, Pending };
  explicit WakeEvent(EventHandle value);
  auto     get() const            -> HANDLE;
  auto     Transition(Phase next) -> void;

private:
  EventHandle        handle;
  std::atomic<Phase> phase { Phase::Idle };
};
}

namespace sdl_rdp::freerdp_facade {
using detail::wake_event::WakeEvent;
}
