#pragma once
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <string>
#include <vector>

namespace sdl_rdp::diagnostics::detail::trace_queue {
using sdl_rdp::utilities::Pinned;

class TraceQueue : private Pinned {
public:
  explicit TraceQueue(Diagnostics const& diagnostics) noexcept;
  auto     Defer(std::string_view event, std::invocable auto&&... fields) -> void {
    if (_diagnostics.Tracing()) _lines.push_back(_diagnostics.Format(event, std::forward<decltype(fields)>(fields)...));
  }
  auto Flush() -> void;

private:
  Diagnostics const&       _diagnostics;
  std::vector<std::string> _lines;
};
}

namespace sdl_rdp::diagnostics {
using detail::trace_queue::TraceQueue;
}
