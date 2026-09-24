#pragma once
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <ranges>

namespace Backend {
using utilities::Expects;
inline auto Rows(sdlrdp_rect area) {
  return std::views::iota(area.y, area.y + area.h)
         | std::views::transform([area](int y) { return sdlrdp_rect{ area.x, y, area.w, 1 }; });
}
constexpr auto SameSize(sdlrdp_rect left, sdlrdp_rect right) noexcept -> bool {
  return left.w == right.w && left.h == right.h;
}
inline auto Merge(std::optional<sdlrdp_rect>& region, sdlrdp_rect area) -> void {
  Expects(area.w > 0, "band width is positive");
  Expects(area.h > 0, "band height is positive");
  if (!region) {
    region = area;
    return;
  }
  auto x      = std::min(region->x, area.x);
  auto y      = std::min(region->y, area.y);
  auto right  = std::max(region->x + region->w, area.x + area.w);
  auto bottom = std::max(region->y + region->h, area.y + area.h);
  region = sdlrdp_rect{ x, y, right - x, bottom - y };
}
inline auto Intersect(sdlrdp_rect left, sdlrdp_rect right) -> std::optional<sdlrdp_rect> {
  Expects(left.w >= 0, "left rectangle width is nonnegative");
  Expects(left.h >= 0, "left rectangle height is nonnegative");
  Expects(right.w >= 0, "right rectangle width is nonnegative");
  Expects(right.h >= 0, "right rectangle height is nonnegative");
  auto x     = std::max(left.x, right.x);
  auto y     = std::max(left.y, right.y);
  auto end_x = std::min(std::int64_t(left.x) + left.w, std::int64_t(right.x) + right.w);
  auto end_y = std::min(std::int64_t(left.y) + left.h, std::int64_t(right.y) + right.h);
  if (end_x <= x || end_y <= y) return std::nullopt;
  return sdlrdp_rect{ x, y, int(end_x - x), int(end_y - y) };
}
}
