#pragma once
#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::updates {
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Rect;

enum class FrameAction : std::uint8_t { Begin, End };
enum class PixelFormat : std::uint8_t { Bgr24, Rgb16 };
struct SurfaceCommand {
  Rect                       area    { };
  std::uint32_t              codec_id{ };
  std::span<std::byte const> payload;
};
struct Bitmap {
  Rect                       area      { };
  std::span<std::byte const> payload;
  std::uint32_t              depth     { 32 };
  bool                       compressed{ };
};
struct PointerImage {
  Extent                        size  { };
  std::uint32_t                 hot_x { };
  std::uint32_t                 hot_y { };
  std::span<std::uint8_t const> pixels;
  std::span<std::uint8_t const> mask;
};
class Updates : private Pinned {
public:
  explicit Updates(Connection& connection) noexcept;
  auto     FrameMarker(FrameAction action, std::uint32_t frame) -> bool;
  auto     SurfaceBits(SurfaceCommand const& command)           -> bool;
  auto     Bitmaps(std::span<Bitmap const> bitmaps)             -> bool;
  auto     DesktopResize()                                      -> bool;
  auto     Pointer(PointerImage const& image)                   -> bool;
  auto     LargePointer(PointerImage const& image)              -> bool;
  auto     HidePointer()                                        -> bool;

private:
  rdp_context& _context;
};
// Converts BGRX rows into a bitmap update's rows of format, each padded to four bytes; empty when FreeRDP refuses.
auto ConvertPixels(std::span<std::uint8_t const> bgrx, Extent size, PixelFormat format) -> std::vector<std::byte>;
}

namespace sdl_rdp::freerdp_facade {
using detail::updates::Bitmap;
using detail::updates::ConvertPixels;
using detail::updates::FrameAction;
using detail::updates::PixelFormat;
using detail::updates::PointerImage;
using detail::updates::SurfaceCommand;
using detail::updates::Updates;
}
