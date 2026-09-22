#pragma once
#include "sdl-rdp-backend.h"
#include "contract.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>

namespace Backend {
using utilities::Expects;
inline void Merge(std::optional<sdlrdp_rect>& region, sdlrdp_rect area)
{
  Expects(area.w > 0 && area.h > 0, "damage has positive extent");
  if (!region) { region = area; return; }
  auto x = std::min(region->x, area.x), y = std::min(region->y, area.y);
  auto right = std::max(region->x + region->w, area.x + area.w);
  auto bottom = std::max(region->y + region->h, area.y + area.h);
  region = sdlrdp_rect{x, y, right - x, bottom - y};
}
inline std::optional<sdlrdp_rect> Intersect(sdlrdp_rect left, sdlrdp_rect right)
{
  Expects(left.w >= 0 && left.h >= 0 && right.w >= 0 && right.h >= 0,
          "rectangles have nonnegative extents");
  auto x = std::max(left.x, right.x), y = std::max(left.y, right.y);
  auto end_x = std::min(std::int64_t(left.x) + left.w, std::int64_t(right.x) + right.w);
  auto end_y = std::min(std::int64_t(left.y) + left.h, std::int64_t(right.y) + right.h);
  if (end_x <= x || end_y <= y) return std::nullopt;
  return sdlrdp_rect{x, y, int(end_x - x), int(end_y - y)};
}
}
