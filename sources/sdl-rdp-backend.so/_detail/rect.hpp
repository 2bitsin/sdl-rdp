#pragma once
#include "contract.hpp"
#include "sdl-rdp-backend.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace Backend {
using utilities::Expects;
inline void Merge(std::optional<sdlrdp_rect>& region, sdlrdp_rect area) {
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
inline std::optional<sdlrdp_rect> Intersect(sdlrdp_rect left, sdlrdp_rect right) {
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
class Region {
public:
  void Add(sdlrdp_rect area) {
    Expects(area.w > 0, "band width is positive");
    Expects(area.h > 0, "band height is positive");
    std::optional<sdlrdp_rect> merged = area;
    for (std::size_t i = 0; i < rects.size();) {
      auto r = rects[i];
      if (r.x <= merged->x + merged->w && merged->x <= r.x + r.w && r.y <= merged->y + merged->h &&
          merged->y <= r.y + r.h) {
        Merge(merged, r);
        rects.erase(rects.begin() + std::ptrdiff_t(i));
        i = 0;
      } else
        ++i;
    }
    rects.push_back(*merged);
    if (rects.size() <= 16) return;
    for (auto r : rects)
      Merge(merged, r);
    rects.assign(1, *merged);
  }
  bool                            empty() const       { return rects.empty(); }
  void                            clear()             { rects.clear(); }
  std::vector<sdlrdp_rect> const& Rects() const       { return rects; }
  void                            Swap(Region& other) { rects.swap(other.rects); }

private:
  std::vector<sdlrdp_rect> rects;
};
}
