#include "_detail/trace-queue.hpp"

#include <algorithm>

namespace Backend {
TraceQueue::TraceQueue(Diagnostics const& diagnostics) noexcept : _diagnostics{ diagnostics } { }
void TraceQueue::Flush() {
  std::ranges::for_each(_lines, [&](auto const& text) { _diagnostics.Emit(text); });
  _lines.clear();
}
}
