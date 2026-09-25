#include <sdl-rdp/picture/frame-layout.hpp>

#include <sdl-rdp/picture/exceptions.hpp>
#include <sdl-rdp/picture/geometry.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <utility>

namespace sdl_rdp::picture::detail::frame_layout {
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::PixelBytes;

namespace {
auto CoveringPitch(Extent size, int pitch) -> std::uint32_t {
  auto const row = std::size_t{ size.width } * PixelBytes;
  if (std::cmp_less(pitch, row)) throw ShortPitch{ pitch, row };
  return Narrowed<std::uint32_t>(pitch);
}
}
FrameLayout::FrameLayout(std::uint32_t width, std::uint32_t height, int pitch)
    : _size{ Dimensions(width, height) }, _pitch{ CoveringPitch(_size, pitch) } { }
auto FrameLayout::Size() const noexcept -> Extent {
  return _size;
}
auto FrameLayout::Pitch() const noexcept -> std::uint32_t {
  return _pitch;
}
// The last row ends at its pixels, not at the pitch.
auto FrameLayout::Bytes() const noexcept -> std::size_t {
  return (std::size_t{ _pitch } * (_size.height - 1)) + (std::size_t{ _size.width } * PixelBytes);
}
}
