#pragma once
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/utilities/deadline.hpp>

#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <vector>

namespace sdl_rdp::link::detail::event_queue {
using sdl_rdp::utilities::Deadline;

class EventQueue {
public:
  auto Push(Event event)              -> void;
  auto Poll()                         -> std::vector<Event>;
  auto Poll(std::vector<Event>& into) -> void;
  auto Wait(Deadline deadline)        -> bool;
  auto Wakeup()                       -> void;

private:
  auto Notify(std::invocable auto change) -> void {
    {
      std::scoped_lock const lock(_guard);
      change();
    }
    _changed.notify_all();
  }
  std::mutex              _guard;
  std::condition_variable _changed;
  std::vector<Event>      _events;
  std::uint64_t           _generation{ };
};
}

namespace sdl_rdp::link {
using detail::event_queue::EventQueue;
}
