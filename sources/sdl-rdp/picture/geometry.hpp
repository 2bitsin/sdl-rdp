#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/utilities/extent.hpp>

#include <cstdint>
#include <limits>
#include <span>

namespace sdl_rdp::picture::detail::geometry {
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::PixelBytes;

// A 32-bit row must fit BitmapUpdate bitmapLength (UINT16); height is UINT16.
inline constexpr std::uint32_t MaximumPictureWidth  = std::numeric_limits<std::uint16_t>::max() / PixelBytes;
inline constexpr std::uint32_t MaximumPictureHeight = std::numeric_limits<std::uint16_t>::max();
auto Dimensions(std::uint32_t width, std::uint32_t height)            -> Extent;
auto ValidateDamage(std::span<sdlrdp_rect const> damage, Extent size) -> void;
class PictureGeometry {
public:
       PictureGeometry(Extent size, sdlrdp_aspect aspect);
  auto Desktop() const                -> sdlrdp_rect;
  auto Desktop(Extent size) const     -> sdlrdp_rect;
  auto Bounds() const noexcept        -> sdlrdp_rect;
  auto Resize(Extent size)            -> bool;
  auto SetAspect(sdlrdp_aspect value) -> void;

private:
  Extent        _size;
  sdlrdp_aspect _aspect;
};
auto Aligned(std::uint32_t dimension) -> std::uint32_t;
}

namespace sdl_rdp::picture {
using detail::geometry::Aligned;
using detail::geometry::Dimensions;
using detail::geometry::PictureGeometry;
using detail::geometry::ValidateDamage;
}
