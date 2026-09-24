#include "_detail/desktop-layout.hpp"

#include "_detail/contract.hpp"
#include "_detail/rect.hpp"

#include <cstdint>
#include <utility>

namespace Backend {
auto ApplyDesktopSize(rdpSettings& settings, sdlrdp_rect picture) -> bool {
  return freerdp_settings_set_uint32(&settings, FreeRDP_DesktopWidth, picture.w) &&
         freerdp_settings_set_uint32(&settings, FreeRDP_DesktopHeight, picture.h);
}
auto DesktopLayout::Rect() const noexcept -> sdlrdp_rect {
  return _desktop;
}
auto DesktopLayout::Assign(sdlrdp_rect value) noexcept -> void {
  _desktop = value;
}
auto DesktopLayout::RecordScreen(rdpSettings const& settings) -> void {
  _screen_width  = freerdp_settings_get_uint32(&settings, FreeRDP_DesktopWidth);
  _screen_height = freerdp_settings_get_uint32(&settings, FreeRDP_DesktopHeight);
}
auto DesktopLayout::ScreenEvent() const noexcept -> sdlrdp_event {
  return { .type = SDLRDP_SCREEN, .screen = { .width = _screen_width, .height = _screen_height } };
}
auto DesktopLayout::Resizing() const noexcept -> bool {
  return _resizing;
}
auto DesktopLayout::BeginResize(sdlrdp_rect picture) noexcept -> void {
  _desktop  = picture;
  _resizing = true;
}
auto DesktopLayout::EndResize() noexcept -> bool {
  return std::exchange(_resizing, false);
}
auto DesktopLayout::Matches(sdlrdp_rect picture) const noexcept -> bool {
  return SameSize(picture, _desktop);
}
auto DesktopLayout::Offer(sdlrdp_rect picture) const noexcept -> sdlrdp_rect {
  return _resizing ? _desktop : picture;
}
auto DesktopLayout::Scale(int value, unsigned target, int sdlrdp_rect::* extent) const -> int {
  Expects(_desktop.*extent > 0, "desktop extent is positive");
  return int(int64_t(value) * target / (_desktop.*extent));
}
}
