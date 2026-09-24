#pragma once
#include <chrono>
#include <format>
#include <string_view>
#include <utility>

namespace Backend {
struct State;
struct Trace {
public:
         Trace(Trace const&)       = delete;
         Trace(Trace&&)            = delete;
         Trace(State& state, bool tracing) : owner{ state }, enabled{ tracing } { }
         ~Trace()                  = default;
  Trace& operator = (Trace const&) = delete;
  Trace& operator = (Trace&&)      = delete;
  bool   Enabled() const { return enabled; }
  void   Line(std::string_view event, auto&&... fields) const {
    auto text = Format(event, std::forward<decltype(fields)>(fields)...);
    if (!text.empty()) Emit(text);
  }
  void        Emit(std::string const& text) const;
  std::string Format(std::string_view event, auto&&... fields) const {
    if (!enabled) return { };
    auto time =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    auto text = std::format("trace {} t={}", event, time);
    ((text += std::format(" {}", std::forward<decltype(fields)>(fields)())), ...);
    return text;
  }

private:
  State& owner;
  bool   enabled;
};
} // namespace Backend
