#include <sdl-rdp/picture/geometry.hpp>

#include <sdl-rdp/picture/exceptions.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <oxbox/utilities/bits.hpp>
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <utility>

namespace sdl_rdp::picture::detail::geometry {
using sdl_rdp::utilities::AspectRatio;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::OutOfRange;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::Whole;

namespace {
auto Inside(Rect area, Extent size) -> bool {
  return area.x >= 0 && area.y >= 0 && area.w > 0 && area.h > 0
         && std::cmp_less_equal(std::int64_t{ area.x } + area.w, size.width)
         && std::cmp_less_equal(std::int64_t{ area.y } + area.h, size.height);
}
}
auto Dimensions(std::uint32_t width, std::uint32_t height) -> Extent {
  if (!width || width > MaximumPictureWidth) throw OutOfRange{ "Desktop width", width, 1, MaximumPictureWidth };
  if (!height || height > MaximumPictureHeight) throw OutOfRange{ "Desktop height", height, 1, MaximumPictureHeight };
  return { .width = width, .height = height };
}
auto ValidateDamage(std::span<Rect const> damage, Extent size) -> void {
  if (!std::ranges::all_of(damage, [=](Rect area) { return Inside(area, size); })) throw DamageOutOfBounds{ };
}
PictureGeometry::PictureGeometry(Extent size, std::optional<AspectRatio> aspect) : _size{ size }, _aspect{ aspect } {
  if (aspect) {
    Expects(aspect->numerator > 0, "an aspect ratio's numerator is positive");
    Expects(aspect->denominator > 0, "an aspect ratio's denominator is positive");
  }
  std::ignore = Desktop();
}
auto PictureGeometry::Desktop() const -> Rect {
  return Desktop(_size);
}
auto PictureGeometry::Desktop(Extent size) const -> Rect {
  Expects(size.width > 0, "shadow width is positive");
  Expects(size.height > 0, "shadow height is positive");
  if (!_aspect) return Whole(size);
  auto                divisor = std::gcd(_aspect->numerator, _aspect->denominator);
  std::uint64_t const n       = _aspect->numerator / divisor;
  std::uint64_t const d       = _aspect->denominator / divisor;
  auto                units   = std::max((size.width + n - 1) / n, (size.height + d - 1) / d);
  if (units * n > MaximumPictureWidth || units * d > MaximumPictureHeight)
    throw DesktopExceedsLimits{ units * n, units * d };
  return Whole({ .width = Narrowed<std::uint32_t>(units * n), .height = Narrowed<std::uint32_t>(units * d) });
}
auto PictureGeometry::Bounds() const noexcept -> Rect {
  return Whole(_size);
}
auto PictureGeometry::Resize(Extent size) -> bool {
  std::ignore = Desktop(size);
  if (_size.width == size.width && _size.height == size.height) return false;
  _size = size;
  return true;
}
auto PictureGeometry::SetAspect(std::optional<AspectRatio> value) -> void {
  auto const previous = std::exchange(_aspect, value);
  try {
    std::ignore = Desktop();
  } catch (...) {
    _aspect = previous;
    throw;
  }
}
auto Aligned(std::uint32_t dimension) -> std::uint32_t {
  Expects(dimension > 0, "surface dimension is positive");
  Expects(dimension <= 32766, "surface dimension fits the graphics protocol");
  return oxbox::utilities::AlignUp<16>(dimension);
}
}
