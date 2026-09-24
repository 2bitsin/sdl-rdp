#pragma once
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <freerdp/settings.h>

namespace Backend {
auto ApplyDesktopSize(rdpSettings& settings, sdlrdp_rect picture) -> bool;
class DesktopLayout {
public:
  auto Rect() const noexcept                                              -> sdlrdp_rect;
  auto Assign(sdlrdp_rect value) noexcept                                 -> void;
  auto RecordScreen(rdpSettings const& settings)                          -> void;
  auto ScreenEvent() const noexcept                                       -> sdlrdp_event;
  auto Resizing() const noexcept                                          -> bool;
  auto BeginResize(sdlrdp_rect picture) noexcept                          -> void;
  auto EndResize() noexcept                                               -> bool;
  auto Matches(sdlrdp_rect picture) const noexcept                        -> bool;
  auto Offer(sdlrdp_rect picture) const noexcept                          -> sdlrdp_rect;
  auto Scale(int value, unsigned target, int sdlrdp_rect::* extent) const -> int;

private:
  sdlrdp_rect _desktop      { };
  unsigned    _screen_width { };
  unsigned    _screen_height{ };
  bool        _resizing     { };
};
}
