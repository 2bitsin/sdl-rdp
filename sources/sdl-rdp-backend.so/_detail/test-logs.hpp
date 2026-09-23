#pragma once
#include "../sdl-rdp-backend.h"
#include <algorithm>
#include <mutex>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

namespace Headless {
struct Logs {
public:
  static void Collect(void* user, sdlrdp_log_level level, const char* text)
  {
    auto& self = *static_cast<Logs*>(user);
    std::scoped_lock const lock(self.guard);
    self.lines.emplace_back(level, text);
  }
  std::string Text(bool include_info = false)
  {
    std::scoped_lock const lock(guard);
    return lines | std::views::filter([=](auto const& line) { return include_info || line.first != SDLRDP_LOG_INFO; }) | std::views::transform([](auto const& line) {
             auto level = line.first == SDLRDP_LOG_ERROR ? "ERROR" : line.first == SDLRDP_LOG_WARN ? "WARN"
                                                                                                   : "INFO";
             return std::string(level) + ": " + line.second;
           }) |
           std::views::join_with('\n') | std::ranges::to<std::string>();
  }
  unsigned Count(sdlrdp_log_level level, std::string_view text)
  {
    std::scoped_lock const lock(guard);
    return std::ranges::count_if(lines, [=](auto const& line) {
      return line.first == level && line.second.contains(text);
    });
  }
  bool Contains(sdlrdp_log_level level, std::string_view text)
  {
    std::scoped_lock const lock(guard);
    return std::ranges::any_of(lines, [=](auto const& line) {
      return line.first == level && line.second.contains(text);
    });
  }
  bool Contains(std::string_view text)
  {
    std::scoped_lock const lock(guard);
    return std::ranges::any_of(lines, [=](auto const& line) { return line.second.contains(text); });
  }
  std::mutex                                            guard;
  std::vector<std::pair<sdlrdp_log_level, std::string>> lines;
};
}
