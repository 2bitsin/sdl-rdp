#pragma once
#include <sdl-rdp/abi/backend.h>

#include <freerdp/settings.h>
#include <cstdint>

namespace Backend {
auto ApplyDesktopSize(rdpSettings& settings, sdlrdp_rect picture) -> bool;
auto Rescale(int value, int extent, std::uint32_t target)         -> int;
class DesktopLayout {
public:
  auto Rect() const noexcept                       -> sdlrdp_rect;
  auto Assign(sdlrdp_rect value) noexcept          -> void;
  auto RecordScreen(rdpSettings const& settings)   -> void;
  auto ScreenEvent() const noexcept                -> sdlrdp_event;
  auto Resizing() const noexcept                   -> bool;
  auto BeginResize(sdlrdp_rect picture) noexcept   -> void;
  auto EndResize() noexcept                        -> bool;
  auto Matches(sdlrdp_rect picture) const noexcept -> bool;
  auto Offer(sdlrdp_rect picture) const noexcept   -> sdlrdp_rect;

private:
  sdlrdp_rect   _desktop      { };
  std::uint32_t _screen_width { };
  std::uint32_t _screen_height{ };
  bool          _resizing     { };
};
}
