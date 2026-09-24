#include "_detail/region.hpp"

#include "_detail/contract.hpp"
#include "_detail/rect.hpp"

#include <cstddef>
#include <optional>

namespace Backend {
void Region::Add(sdlrdp_rect area) {
  Expects(area.w > 0, "band width is positive");
  Expects(area.h > 0, "band height is positive");
  std::optional<sdlrdp_rect> merged{ area };
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
bool Region::empty() const {
  return rects.empty();
}
void Region::clear() {
  rects.clear();
}
std::vector<sdlrdp_rect> const& Region::Rects() const {
  return rects;
}
void Region::Swap(Region& other) {
  rects.swap(other.rects);
}
}
