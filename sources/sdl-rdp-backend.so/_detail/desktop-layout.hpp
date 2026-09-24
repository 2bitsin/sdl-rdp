#pragma once
#include "sdl-rdp-backend.h"

#include <freerdp/settings.h>

namespace Backend {
bool ApplyDesktopSize(rdpSettings& settings, sdlrdp_rect picture);
class DesktopLayout {
public:
  sdlrdp_rect  Rect() const                       noexcept;
  void         Assign(sdlrdp_rect value)          noexcept;
  void         RecordScreen(rdpSettings const& settings);
  sdlrdp_event ScreenEvent() const                noexcept;
  bool         Resizing() const                   noexcept;
  void         BeginResize(sdlrdp_rect picture)   noexcept;
  bool         EndResize()                        noexcept;
  bool         Matches(sdlrdp_rect picture) const noexcept;
  sdlrdp_rect  Offer(sdlrdp_rect picture) const   noexcept;
  int          Scale(int value, unsigned target, int sdlrdp_rect::* extent) const;

private:
  sdlrdp_rect _desktop      { };
  unsigned    _screen_width { };
  unsigned    _screen_height{ };
  bool        _resizing     { };
};
}
