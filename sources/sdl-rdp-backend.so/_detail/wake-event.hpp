#pragma once
#include "rdp-handles.hpp"
#include <atomic>

namespace Backend {
class WakeEvent {
public:
  enum class Phase { Idle, Pending };
  explicit WakeEvent(HANDLE value);
  HANDLE get() const;
  explicit operator bool() const;
  void Transition(Phase next);
private:
  EventHandle        handle;
  std::atomic<Phase> phase  { Phase::Idle };
};
}
