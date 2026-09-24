#pragma once
#include "sdl-rdp-backend.h"

#include <concepts>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <span>

namespace Backend {
class EventQueue {
public:
  void     Push(sdlrdp_event event);
  unsigned Poll(std::span<sdlrdp_event> out);
  int      Wait(int timeout);
  void     Wakeup();

private:
  void Notify(std::invocable auto change) {
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
