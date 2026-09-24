#include <sdl-rdp/core/event-queue.hpp>

#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace Backend {
auto EventQueue::Push(sdlrdp_event event) -> void {
  Notify([&] { _events.push_back(event); });
}
auto EventQueue::Poll(std::span<sdlrdp_event> out) -> std::uint32_t {
  std::scoped_lock const lock(_guard);
  auto const             count = std::min(out.size(), _events.size());
  std::copy_n(_events.begin(), count, out.begin());
  _events.erase(_events.begin(), _events.begin() + Narrowed<std::ptrdiff_t>(count));
  return Narrowed<std::uint32_t>(count);
}
auto EventQueue::Wait(Deadline deadline) -> int {
  std::unique_lock lock(_guard);
  auto const       since = _generation;
  auto const       ready = [&] { return !_events.empty() || since != _generation; };
  _changed.wait_until(lock, deadline, ready);
  return !_events.empty();
}
auto EventQueue::Wakeup() -> void {
  Notify([&] { ++_generation; });
}
}
