#pragma once
#include <sdl-rdp/core/diagnostics.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <string>
#include <vector>

namespace Backend {
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
