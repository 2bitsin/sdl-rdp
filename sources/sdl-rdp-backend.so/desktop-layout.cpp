#include "_detail/desktop-layout.hpp"

#include "_detail/contract.hpp"
#include "_detail/rect.hpp"

#include <cstdint>
#include <utility>

namespace Backend {
bool ApplyDesktopSize(rdpSettings& settings, sdlrdp_rect picture) {
  return freerdp_settings_set_uint32(&settings, FreeRDP_DesktopWidth, picture.w) &&
         freerdp_settings_set_uint32(&settings, FreeRDP_DesktopHeight, picture.h);
}
sdlrdp_rect DesktopLayout::Rect() const noexcept {
  return _desktop;
}
void DesktopLayout::Assign(sdlrdp_rect value) noexcept {
  _desktop = value;
}
void DesktopLayout::RecordScreen(rdpSettings const& settings) {
  _screen_width  = freerdp_settings_get_uint32(&settings, FreeRDP_DesktopWidth);
  _screen_height = freerdp_settings_get_uint32(&settings, FreeRDP_DesktopHeight);
}
sdlrdp_event DesktopLayout::ScreenEvent() const noexcept {
  return { .type = SDLRDP_SCREEN, .screen = { .width = _screen_width, .height = _screen_height } };
}
bool DesktopLayout::Resizing() const noexcept {
  return _resizing;
}
void DesktopLayout::BeginResize(sdlrdp_rect picture) noexcept {
  _desktop  = picture;
  _resizing = true;
}
bool DesktopLayout::EndResize() noexcept {
  return std::exchange(_resizing, false);
}
bool DesktopLayout::Matches(sdlrdp_rect picture) const noexcept {
  return SameSize(picture, _desktop);
}
sdlrdp_rect DesktopLayout::Offer(sdlrdp_rect picture) const noexcept {
  return _resizing ? _desktop : picture;
}
int DesktopLayout::Scale(int value, unsigned target, int sdlrdp_rect::* extent) const {
  Expects(_desktop.*extent > 0, "desktop extent is positive");
  return int(int64_t(value) * target / (_desktop.*extent));
}
}
