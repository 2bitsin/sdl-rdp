#include <sdl-rdp/picture/geometry.hpp>

#include <sdl-rdp/picture/exceptions.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <oxbox/utilities/bits.hpp>
#include <algorithm>
#include <cstdint>
#include <numeric>
#include <utility>

namespace Backend {
PictureGeometry::PictureGeometry(Extent size, sdlrdp_aspect aspect) : _size{ size }, _aspect{ aspect } {
  std::ignore = Desktop();
}
auto PictureGeometry::Desktop() const -> sdlrdp_rect {
  return Desktop(_size);
}
auto PictureGeometry::Desktop(Extent size) const -> sdlrdp_rect {
  Expects(size.width > 0, "shadow width is positive");
  Expects(size.height > 0, "shadow height is positive");
  if (!_aspect.num || !_aspect.den) return Whole(size);
  auto                divisor = std::gcd(_aspect.num, _aspect.den);
  std::uint64_t const n       = _aspect.num / divisor;
  std::uint64_t const d       = _aspect.den / divisor;
  auto                units   = std::max((size.width + n - 1) / n, (size.height + d - 1) / d);
  if (units * n > MaximumPictureWidth || units * d > MaximumPictureHeight)
    throw sdl_rdp::picture::DesktopExceedsLimits{ units * n, units * d };
  return Whole({ .width = Narrowed<std::uint32_t>(units * n), .height = Narrowed<std::uint32_t>(units * d) });
}
auto PictureGeometry::Bounds() const noexcept -> sdlrdp_rect {
  return Whole(_size);
}
auto PictureGeometry::Resize(Extent size) -> bool {
  std::ignore = Desktop(size);
  if (_size.width == size.width && _size.height == size.height) return false;
  _size = size;
  return true;
}
auto PictureGeometry::SetAspect(sdlrdp_aspect value) -> void {
  auto const previous = std::exchange(_aspect, value);
  try {
    std::ignore = Desktop();
  } catch (...) {
    _aspect = previous;
    throw;
  }
}
}

namespace Backend::Avc {
auto Aligned(std::uint32_t dimension) -> std::uint32_t {
  Expects(dimension > 0, "surface dimension is positive");
  Expects(dimension <= 32766, "surface dimension fits the graphics protocol");
  return oxbox::utilities::AlignUp<16>(dimension);
}
}
