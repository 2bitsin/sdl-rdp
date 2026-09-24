#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>

#include <atomic>

namespace Backend {
auto Signalled(HANDLE event) -> bool;
class WakeEvent {
public:
  enum class Phase{ Idle, Pending };
  explicit WakeEvent(HANDLE value);
  auto     get() const            -> HANDLE;
  explicit operator bool() const;
  auto     Transition(Phase next) -> void;

private:
  EventHandle        handle;
  std::atomic<Phase> phase { Phase::Idle };
};
}
