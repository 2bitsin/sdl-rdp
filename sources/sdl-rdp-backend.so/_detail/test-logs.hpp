#pragma once
#include "../sdl-rdp-backend.h"
#include "contract.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Headless {
class Logs {
public:
  static auto Collect(void* user, sdlrdp_log_level level, char const* text) -> void;
  auto Text(bool include_info = false)                         -> std::string;
  auto Count(sdlrdp_log_level level, std::string_view text)    -> unsigned;
  auto Contains(sdlrdp_log_level level, std::string_view text) -> bool;
  auto Contains(std::string_view text)                         -> bool;
  auto Entries()                                               -> std::vector<std::pair<sdlrdp_log_level, std::string>>;
  template <typename Observe>
  auto Follow(std::size_t next, Observe observe) -> std::size_t {
    std::scoped_lock const lock(guard);
    return FollowLocked(next, observe);
  }
  template <typename Observe, typename Ready>
  auto WaitAfter(std::size_t next, Observe observe, Ready ready) -> std::optional<std::size_t> {
    std::unique_lock lock(guard);
    auto             complete = changed.wait_for(lock, std::chrono::seconds(15), [&] {
      next = FollowLocked(next, observe);
      return ready();
    });
    return complete ? std::optional(next) : std::nullopt;
  }

private:
  template <typename Observe>
  auto FollowLocked(std::size_t next, Observe& observe) -> std::size_t {
    utilities::Expects(next <= lines.size(), "log cursor is within the collected trace");
    std::ranges::for_each(std::span(lines).subspan(next), [&](auto const& entry) { observe(entry.second); });
    return lines.size();
  }
  template <typename Projection>
  auto Matching(Projection project) -> unsigned {
    std::scoped_lock const lock(guard);
    return std::ranges::count_if(lines, project);
  }
  std::condition_variable                               changed;
  std::mutex                                            guard;
  std::vector<std::pair<sdlrdp_log_level, std::string>> lines;
};
}
