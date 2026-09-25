#include <sdl-rdp/link/event-queue.hpp>

#include <utility>

namespace sdl_rdp::link::detail::event_queue {
auto EventQueue::Push(Event event) -> void {
  Notify([&] { _events.push_back(std::move(event)); });
}
auto EventQueue::Poll() -> std::vector<Event> {
  std::vector<Event> events;
  Poll(events);
  return events;
}
// The caller's buffer becomes the queue's, so a steady stream reuses two buffers and allocates nothing.
auto EventQueue::Poll(std::vector<Event>& into) -> void {
  into.clear();
  std::scoped_lock const lock(_guard);
  std::swap(into, _events);
}
auto EventQueue::Wait(Deadline deadline) -> bool {
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
