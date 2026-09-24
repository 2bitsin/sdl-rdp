#include "_detail/picture-geometry.hpp"

#include "_detail/contract.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace Backend {
PictureGeometry::PictureGeometry(Extent size, sdlrdp_aspect aspect) : _size{ size }, _aspect{ aspect } {
  std::ignore = Desktop();
}
sdlrdp_rect PictureGeometry::Desktop() const {
  return Desktop(_size);
}
sdlrdp_rect PictureGeometry::Desktop(Extent size) const {
  Expects(size.width > 0, "shadow width is positive");
  Expects(size.height > 0, "shadow height is positive");
  if (!_aspect.num || !_aspect.den) return Whole(size);
  auto           divisor = std::gcd(_aspect.num, _aspect.den);
  uint64_t const n       = _aspect.num / divisor;
  uint64_t const d       = _aspect.den / divisor;
  auto           units   = std::max((size.width + n - 1) / n, (size.height + d - 1) / d);
  if (units * n > MaximumPictureWidth || units * d > MaximumPictureHeight)
    throw std::runtime_error("Aspect-corrected desktop exceeds RDP dimensions.");
  return Whole({ .width = unsigned(units * n), .height = unsigned(units * d) });
}
sdlrdp_rect PictureGeometry::Bounds() const noexcept {
  return Whole(_size);
}
bool PictureGeometry::Resize(Extent size) {
  std::ignore = Desktop(size);
  if (_size.width == size.width && _size.height == size.height) return false;
  _size = size;
  return true;
}
void PictureGeometry::SetAspect(sdlrdp_aspect value) {
  auto const previous = std::exchange(_aspect, value);
  try {
    std::ignore = Desktop();
  } catch (...) {
    _aspect = previous;
    throw;
  }
}
}
