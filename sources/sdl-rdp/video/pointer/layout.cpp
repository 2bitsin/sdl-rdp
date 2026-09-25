#include <sdl-rdp/video/pointer/layout.hpp>

#include <sdl-rdp/video/exceptions.hpp>

namespace sdl_rdp::video::pointer::detail::layout {
using sdl_rdp::utilities::PixelBytes;

namespace {
auto Valid(Extent size, std::uint32_t x, std::uint32_t y) -> bool {
  if (size.width > LargePointerLimit || size.height > LargePointerLimit) return false;
  if (!size.width && !size.height) return true;
  return size.width && size.height && x < size.width && y < size.height;
}
auto Checked(Extent size, std::uint32_t x, std::uint32_t y) -> Extent {
  if (!Valid(size, x, y)) throw InvalidPointerLayout{ size.width, size.height, x, y };
  return size;
}
}
PointerLayout::PointerLayout(Extent size, std::uint32_t x, std::uint32_t y)
    : _size{ Checked(size, x, y) }, _x{ x }, _y{ y } { }
auto PointerLayout::Size() const noexcept -> Extent {
  return _size;
}
auto PointerLayout::X() const noexcept -> std::uint32_t {
  return _x;
}
auto PointerLayout::Y() const noexcept -> std::uint32_t {
  return _y;
}
auto PointerLayout::Bytes() const noexcept -> std::size_t {
  return std::size_t{ _size.width } * _size.height * PixelBytes;
}
}
