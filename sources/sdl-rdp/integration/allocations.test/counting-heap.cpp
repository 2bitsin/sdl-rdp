#include "counting-heap.hpp"

namespace HeapCount {
thread_local bool CountingHeap::suspended = false;

auto CountingHeap::Shared() noexcept -> CountingHeap& {
  static CountingHeap shared;
  return shared;
}
auto CountingHeap::Suspend() noexcept -> bool {
  return std::exchange(suspended, true);
}
auto CountingHeap::Restore(bool previous) noexcept -> void {
  suspended = previous;
}
auto CountingHeap::CountNew() noexcept -> void {
  if (!suspended) news.fetch_add(1, std::memory_order_relaxed);
}
auto CountingHeap::CountHeap() noexcept -> void {
  if (!suspended) heap.fetch_add(1, std::memory_order_relaxed);
}
auto CountingHeap::Current() const noexcept -> Tally {
  return { .news = news.load(std::memory_order_relaxed), .heap = heap.load(std::memory_order_relaxed) };
}
}
