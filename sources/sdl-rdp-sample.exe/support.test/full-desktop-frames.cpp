#include "support.test/full-desktop-frames.hpp"

#include <algorithm>
#include <functional>
#include <utility>

namespace SampleGate {
FullDesktopFrames::FullDesktopFrames(Headless::Client& client)
    : hook(client, std::bind_front(&FullDesktopFrames::Observe, this)) { }
auto FullDesktopFrames::Full() const -> unsigned {
  return full;
}
auto FullDesktopFrames::Deliveries() const -> unsigned {
  return deliveries;
}
auto FullDesktopFrames::Observe(Headless::PictureUpdate const& update) -> void {
  if (!update.delivered) return;
  for (auto region : update.regions) Cover(region, update.desktop);
}
auto FullDesktopFrames::Cover(sdlrdp_rect region, Backend::Extent desktop) -> void {
  ++deliveries;
  rows.resize(desktop.height);
  auto const bottom = region.y + region.h;
  if (region.x || std::cmp_not_equal(region.x + region.w, desktop.width) || region.h <= 0 ||
      std::cmp_greater(bottom, rows.size()))
    return;
  std::fill(rows.begin() + region.y, rows.begin() + bottom, true);
  if (std::ranges::all_of(rows, [](bool covered) { return covered; })) {
    ++full;
    std::ranges::fill(rows, false);
  }
}
}
