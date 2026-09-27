#include <sdl-rdp/picture/desktop-layout.hpp>

#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <cstdint>
#include <utility>

namespace sdl_rdp::picture::detail::desktop_layout {
using sdl_rdp::freerdp_facade::NumberKey;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::SameSize;

auto ApplyDesktopSize(SettingsView settings, Rect picture) -> void {
  settings.Set(NumberKey::DesktopWidth, Narrowed<std::uint32_t>(picture.w));
  settings.Set(NumberKey::DesktopHeight, Narrowed<std::uint32_t>(picture.h));
}
auto DesktopLayout::Desktop() const noexcept -> Rect {
  return _desktop;
}
auto DesktopLayout::Assign(Rect value) noexcept -> void {
  _desktop = value;
}
auto DesktopLayout::RecordScreen(SettingsReader settings) -> void {
  _screen_width  = settings.Get(NumberKey::DesktopWidth);
  _screen_height = settings.Get(NumberKey::DesktopHeight);
}
auto DesktopLayout::Screen() const noexcept -> Extent {
  return { .width = _screen_width, .height = _screen_height };
}
auto DesktopLayout::Resizing() const noexcept -> bool {
  return _resizing;
}
auto DesktopLayout::BeginResize(Rect picture) noexcept -> void {
  _desktop  = picture;
  _resizing = true;
}
auto DesktopLayout::EndResize() noexcept -> bool {
  return std::exchange(_resizing, false);
}
auto DesktopLayout::Matches(Rect picture) const noexcept -> bool {
  return SameSize(picture, _desktop);
}
auto DesktopLayout::Offer(Rect picture) const noexcept -> Rect {
  return _resizing ? _desktop : picture;
}
auto Rescale(int value, int extent, std::uint32_t target) -> int {
  Expects(extent > 0, "the scaled extent is positive");
  return Narrowed<int>(std::int64_t{ value } * target / extent);
}
}
