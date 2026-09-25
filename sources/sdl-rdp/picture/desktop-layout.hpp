#pragma once
#include <sdl-rdp/utilities/extent.hpp>
#include <sdl-rdp/utilities/rect.hpp>

#include <freerdp/settings.h>
#include <cstdint>

namespace sdl_rdp::picture::detail::desktop_layout {
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Rect;
auto ApplyDesktopSize(rdpSettings& settings, Rect picture) -> bool;
auto Rescale(int value, int extent, std::uint32_t target)  -> int;
class DesktopLayout {
public:
  auto Desktop() const noexcept                  -> Rect;
  auto Assign(Rect value) noexcept               -> void;
  auto RecordScreen(rdpSettings const& settings) -> void;
  auto Screen() const noexcept                   -> Extent;
  auto Resizing() const noexcept                 -> bool;
  auto BeginResize(Rect picture) noexcept        -> void;
  auto EndResize() noexcept                      -> bool;
  auto Matches(Rect picture) const noexcept      -> bool;
  auto Offer(Rect picture) const noexcept        -> Rect;

private:
  Rect          _desktop      { };
  std::uint32_t _screen_width { };
  std::uint32_t _screen_height{ };
  bool          _resizing     { };
};
}

namespace sdl_rdp::picture {
using detail::desktop_layout::ApplyDesktopSize;
using detail::desktop_layout::DesktopLayout;
using detail::desktop_layout::Rescale;
}
