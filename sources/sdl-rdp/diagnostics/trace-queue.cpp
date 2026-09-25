#include <sdl-rdp/diagnostics/trace-queue.hpp>

#include <algorithm>

namespace sdl_rdp::diagnostics::detail::trace_queue {
TraceQueue::TraceQueue(Diagnostics const& diagnostics) noexcept : _diagnostics{ diagnostics } { }
auto TraceQueue::Flush() -> void {
  std::ranges::for_each(_lines, [&](auto const& text) { _diagnostics.Emit(text); });
  _lines.clear();
}
}
