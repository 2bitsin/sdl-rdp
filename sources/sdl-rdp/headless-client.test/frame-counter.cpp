#include <sdl-rdp/headless-client.test/frame-counter.hpp>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <utility>

namespace BackendGate {
FrameCounter::FrameCounter(Headless::Client& client) : hook(client, std::bind_front(&FrameCounter::Count, this)) { }
auto FrameCounter::Frames() const -> std::size_t {
  return frames;
}
auto FrameCounter::BitmapPdus() const -> std::size_t {
  return bitmap_pdus;
}
auto FrameCounter::Count(Headless::PictureUpdate const& update) -> void {
  if (update.command == Headless::PictureCommand::Bitmap) ++bitmap_pdus;
  auto const reaches_bottom = [&](sdlrdp_rect region) {
    return std::cmp_equal(region.y + region.h, update.desktop.height);
  };
  if (update.delivered && std::ranges::any_of(update.regions, reaches_bottom)) ++frames;
}
}
