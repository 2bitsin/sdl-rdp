#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/extent.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <ranges>
#include <source_location>

namespace Backend {
using utilities::Expects;
inline auto Rows(sdlrdp_rect area) {
  return std::views::iota(area.y, area.y + area.h)
         | std::views::transform([area](int y) { return sdlrdp_rect{ area.x, y, area.w, 1 }; });
}
constexpr auto SameSize(sdlrdp_rect left, sdlrdp_rect right) noexcept -> bool {
  return left.w == right.w && left.h == right.h;
}
inline auto ExpectsArea(sdlrdp_rect area, std::source_location where = std::source_location::current()) -> void {
  Expects(area.w >= 0, "rectangle width is nonnegative", where);
  Expects(area.h >= 0, "rectangle height is nonnegative", where);
}
inline auto RowBytes(int width) -> std::size_t {
  return Narrowed<std::size_t>(width) * PixelBytes;
}
inline auto AreaBytes(sdlrdp_rect area) -> std::size_t {
  return RowBytes(area.w) * Narrowed<std::size_t>(area.h);
}
inline auto Union(sdlrdp_rect left, sdlrdp_rect right) -> sdlrdp_rect {
  ExpectsArea(left);
  ExpectsArea(right);
  auto const x     = std::min(left.x, right.x);
  auto const y     = std::min(left.y, right.y);
  auto const end_x = std::max(left.x + left.w, right.x + right.w);
  auto const end_y = std::max(left.y + left.h, right.y + right.h);
  return { x, y, end_x - x, end_y - y };
}
inline auto Touches(sdlrdp_rect left, sdlrdp_rect right) -> bool {
  ExpectsArea(left);
  ExpectsArea(right);
  return left.x <= right.x + right.w && right.x <= left.x + left.w && left.y <= right.y + right.h
         && right.y <= left.y + left.h;
}
inline auto ExpectsBand(sdlrdp_rect area, std::source_location where = std::source_location::current()) -> void {
  Expects(area.w > 0, "band width is positive", where);
  Expects(area.h > 0, "band height is positive", where);
}
inline auto Intersect(sdlrdp_rect left, sdlrdp_rect right) -> std::optional<sdlrdp_rect> {
  ExpectsArea(left);
  ExpectsArea(right);
  auto x     = std::max(left.x, right.x);
  auto y     = std::max(left.y, right.y);
  auto end_x = std::min(std::int64_t{ left.x } + left.w, std::int64_t{ right.x } + right.w);
  auto end_y = std::min(std::int64_t{ left.y } + left.h, std::int64_t{ right.y } + right.h);
  if (end_x <= x || end_y <= y) return std::nullopt;
  return sdlrdp_rect{ x, y, Narrowed<int>(end_x - x), Narrowed<int>(end_y - y) };
}
}
