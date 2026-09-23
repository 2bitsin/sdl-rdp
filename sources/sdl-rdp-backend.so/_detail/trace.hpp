#pragma once
#include <chrono>
#include <format>
#include <string_view>
#include <utility>

namespace Backend {
struct State;
struct Trace {
  State& owner;
  bool enabled;
  void Emit(std::string const& text) const;
  void Line(std::string_view event, auto&&... fields) const {
    if (!enabled) return;
    auto time = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
    auto text = std::format("trace {} t={}", event, time);
    ((text += std::format(" {}", std::forward<decltype(fields)>(fields)())), ...);
    Emit(text);
  }
};
}
