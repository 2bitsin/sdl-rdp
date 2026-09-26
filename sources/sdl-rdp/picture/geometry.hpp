#pragma once
#include <sdl-rdp/utilities/geometry.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace sdl_rdp::picture::detail::geometry {
using sdl_rdp::utilities::AspectRatio;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::PixelBytes;
using sdl_rdp::utilities::Rect;

// A 32-bit row must fit BitmapUpdate bitmapLength (UINT16); height is UINT16.
inline constexpr std::uint32_t MaximumPictureWidth  = std::numeric_limits<std::uint16_t>::max() / PixelBytes;
inline constexpr std::uint32_t MaximumPictureHeight = std::numeric_limits<std::uint16_t>::max();
auto Dimensions(std::uint32_t width, std::uint32_t height)     -> Extent;
auto ValidateDamage(std::span<Rect const> damage, Extent size) -> void;
class PictureGeometry {
public:
       PictureGeometry(Extent size, std::optional<AspectRatio> aspect);
  auto Desktop() const                             -> Rect;
  auto Desktop(Extent size) const                  -> Rect;
  auto Bounds() const noexcept                     -> Rect;
  auto Resize(Extent size)                         -> bool;
  auto SetAspect(std::optional<AspectRatio> value) -> void;

private:
  Extent                     _size;
  std::optional<AspectRatio> _aspect;
};
auto Aligned(std::uint32_t dimension) -> std::uint32_t;
}

namespace sdl_rdp::picture {
using detail::geometry::Aligned;
using detail::geometry::Dimensions;
using detail::geometry::PictureGeometry;
using detail::geometry::ValidateDamage;
}
