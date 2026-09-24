#pragma once
#include "diagnostics.hpp"
#include "pinned.hpp"

#include <string>
#include <vector>

namespace Backend {
class TraceQueue : private Pinned {
public:
  explicit TraceQueue(Diagnostics const& diagnostics) noexcept;
  void     Defer(std::string_view event, std::invocable auto&&... fields) {
    if (_diagnostics.Tracing()) _lines.push_back(_diagnostics.Format(event, std::forward<decltype(fields)>(fields)...));
  }
  void Flush();

private:
  Diagnostics const&       _diagnostics;
  std::vector<std::string> _lines;
};
}
