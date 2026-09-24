#include <sdl-rdp/core/event-queue.hpp>

#include <algorithm>
#include <chrono>

namespace Backend {
auto EventQueue::Push(sdlrdp_event event) -> void {
  Notify([&] { _events.push_back(event); });
}
auto EventQueue::Poll(std::span<sdlrdp_event> out) -> unsigned {
  std::scoped_lock const lock(_guard);
  auto const             count = std::min(out.size(), _events.size());
  std::copy_n(_events.begin(), count, out.begin());
  _events.erase(_events.begin(), _events.begin() + std::ptrdiff_t(count));
  return unsigned(count);
}
auto EventQueue::Wait(int timeout) -> int {
  std::unique_lock lock(_guard);
  auto const       since = _generation;
  auto const       ready = [&] { return !_events.empty() || since != _generation; };
  if (timeout < 0)
    _changed.wait(lock, ready);
  else
    _changed.wait_for(lock, std::chrono::milliseconds(timeout), ready);
  return !_events.empty();
}
auto EventQueue::Wakeup() -> void {
  Notify([&] { ++_generation; });
}
}
