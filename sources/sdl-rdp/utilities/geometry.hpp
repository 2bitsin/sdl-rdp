#pragma once
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <ranges>
#include <source_location>

namespace sdl_rdp::utilities::detail::geometry {
inline constexpr std::size_t PixelBytes = 4;
struct Extent {
  std::uint32_t width { };
  std::uint32_t height{ };

  friend constexpr auto operator==(Extent, Extent) noexcept -> bool = default;
};
// gtest prints an Extent through this; a template, so this header needs <iosfwd> only.
template <class CharTy, class TraitsTy>
auto operator<<(std::basic_ostream<CharTy, TraitsTy>& output, Extent size) -> std::basic_ostream<CharTy, TraitsTy>& {
  return output << size.width << CharTy{ 'x' } << size.height;
}
struct AspectRatio {
  std::uint32_t numerator  { };
  std::uint32_t denominator{ };
};
constexpr auto operator==(AspectRatio left, AspectRatio right) noexcept -> bool {
  return left.numerator == right.numerator && left.denominator == right.denominator;
}
struct Rect {
  int x{ };
  int y{ };
  int w{ };
  int h{ };
};
constexpr auto Whole(Extent size) noexcept -> Rect {
  return { .x = 0, .y = 0, .w = Narrowed<int>(size.width), .h = Narrowed<int>(size.height) };
}
constexpr auto SizeOf(Rect area) -> Extent {
  return { .width = Narrowed<std::uint32_t>(area.w), .height = Narrowed<std::uint32_t>(area.h) };
}
inline auto Rows(Rect area) {
  return std::views::iota(area.y, area.y + area.h)
         | std::views::transform([area](int y) { return Rect{ .x = area.x, .y = y, .w = area.w, .h = 1 }; });
}
constexpr auto SameSize(Rect left, Rect right) noexcept -> bool {
  return left.w == right.w && left.h == right.h;
}
inline auto ExpectsArea(Rect area, std::source_location where = std::source_location::current()) -> void {
  Expects(area.w >= 0, "rectangle width is nonnegative", where);
  Expects(area.h >= 0, "rectangle height is nonnegative", where);
}
inline auto RowBytes(int width) -> std::size_t {
  return Narrowed<std::size_t>(width) * PixelBytes;
}
inline auto AreaBytes(Rect area) -> std::size_t {
  return RowBytes(area.w) * Narrowed<std::size_t>(area.h);
}
inline auto Union(Rect left, Rect right) -> Rect {
  ExpectsArea(left);
  ExpectsArea(right);
  auto const x     = std::min(left.x, right.x);
  auto const y     = std::min(left.y, right.y);
  auto const end_x = std::max(left.x + left.w, right.x + right.w);
  auto const end_y = std::max(left.y + left.h, right.y + right.h);
  return { .x = x, .y = y, .w = end_x - x, .h = end_y - y };
}
inline auto Touches(Rect left, Rect right) -> bool {
  ExpectsArea(left);
  ExpectsArea(right);
  return left.x <= right.x + right.w && right.x <= left.x + left.w && left.y <= right.y + right.h
         && right.y <= left.y + left.h;
}
inline auto ExpectsBand(Rect area, std::source_location where = std::source_location::current()) -> void {
  Expects(area.w > 0, "band width is positive", where);
  Expects(area.h > 0, "band height is positive", where);
}
inline auto Intersect(Rect left, Rect right) -> std::optional<Rect> {
  ExpectsArea(left);
  ExpectsArea(right);
  auto x     = std::max(left.x, right.x);
  auto y     = std::max(left.y, right.y);
  auto end_x = std::min(std::int64_t{ left.x } + left.w, std::int64_t{ right.x } + right.w);
  auto end_y = std::min(std::int64_t{ left.y } + left.h, std::int64_t{ right.y } + right.h);
  if (end_x <= x || end_y <= y) return std::nullopt;
  return Rect{ .x = x, .y = y, .w = Narrowed<int>(end_x - x), .h = Narrowed<int>(end_y - y) };
}
}

namespace sdl_rdp::utilities {
using detail::geometry::AreaBytes;
using detail::geometry::AspectRatio;
using detail::geometry::ExpectsArea;
using detail::geometry::ExpectsBand;
using detail::geometry::Extent;
using detail::geometry::PixelBytes;
using detail::geometry::Rect;
using detail::geometry::RowBytes;
using detail::geometry::Rows;
using detail::geometry::SameSize;
using detail::geometry::SizeOf;
using detail::geometry::Touches;
using detail::geometry::Union;
using detail::geometry::Whole;
}
