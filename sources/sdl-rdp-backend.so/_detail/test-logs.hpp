#pragma once
#include "../sdl-rdp-backend.h"
#include "contract.hpp"

#include <algorithm>
#include <mutex>
#include <optional>
#include <condition_variable>
#include <chrono>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Headless {
class Logs {
public:
  static void                                           Collect(void* user, sdlrdp_log_level level, char const* text);
  std::string                                           Text(bool include_info = false);
  unsigned                                              Count(sdlrdp_log_level level, std::string_view text);
  bool                                                  Contains(sdlrdp_log_level level, std::string_view text);
  bool                                                  Contains(std::string_view text);
  std::vector<std::pair<sdlrdp_log_level, std::string>> Entries();
  template <typename Observe>
  std::size_t Follow(std::size_t next, Observe observe) {
    std::scoped_lock const lock(guard);
    return FollowLocked(next, observe);
  }
  template <typename Observe, typename Ready>
  std::optional<std::size_t> WaitAfter(std::size_t next, Observe observe, Ready ready) {
    std::unique_lock lock(guard);
    auto             complete = changed.wait_for(lock, std::chrono::seconds(15), [&] {
      next = FollowLocked(next, observe);
      return ready();
    });
    return complete ? std::optional(next) : std::nullopt;
  }

private:
  template <typename Observe>
  std::size_t FollowLocked(std::size_t next, Observe& observe) {
    utilities::Expects(next <= lines.size(), "log cursor is within the collected trace");
    std::ranges::for_each(std::span(lines).subspan(next), [&](auto const& entry) { observe(entry.second); });
    return lines.size();
  }
  template <typename Projection>
  unsigned Matching(Projection project) {
    std::scoped_lock const lock(guard);
    return std::ranges::count_if(lines, project);
  }
  std::condition_variable                               changed;
  std::mutex                                            guard;
  std::vector<std::pair<sdlrdp_log_level, std::string>> lines;
};
}
