#include <sdl-rdp/utilities/region.hpp>

#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/rect.hpp>

#include <algorithm>
#include <cstddef>

namespace Backend {
namespace {
constexpr std::size_t MaximumRects = 16;
}
auto Region::Add(sdlrdp_rect area) -> void {
  ExpectsBand(area);
  auto const touching = [&] { return std::ranges::find_if(rects, [&](sdlrdp_rect r) { return Touches(r, area); }); };
  for (auto found = touching(); found != rects.end(); found = touching()) {
    area = Union(area, *found);
    rects.erase(found);
  }
  rects.push_back(area);
  if (rects.size() > MaximumRects) rects.assign(1, std::ranges::fold_left(rects, rects.front(), Union));
}
// Clearing and swapping keep the vectors' capacity: the per-present path is gated at zero allocations.
auto Region::Clear() noexcept -> void {
  rects.clear();
}
auto Region::Swap(Region& other) noexcept -> void {
  rects.swap(other.rects);
}
auto Region::Rects() const -> std::vector<sdlrdp_rect> const& {
  return rects;
}
}
