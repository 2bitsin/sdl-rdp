#pragma once
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <concepts>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <span>

namespace Backend {
class EventQueue {
public:
  auto Push(sdlrdp_event event)          -> void;
  auto Poll(std::span<sdlrdp_event> out) -> unsigned;
  auto Wait(Deadline deadline)           -> int;
  auto Wakeup()                          -> void;

private:
  auto Notify(std::invocable auto change) -> void {
    {
      std::scoped_lock const lock(_guard);
      change();
    }
    _changed.notify_all();
  }
  std::mutex               _guard;
  std::condition_variable  _changed;
  std::deque<sdlrdp_event> _events;
  unsigned long            _generation{ };
};
}
