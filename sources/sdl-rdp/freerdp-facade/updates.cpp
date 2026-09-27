#include <sdl-rdp/freerdp-facade/updates.hpp>

#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/codec/color.h>
#include <freerdp/freerdp.h>
#include <freerdp/pointer.h>
#include <freerdp/update.h>
#include <oxbox/utilities/span.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::updates {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Stride;
using sdl_rdp::utilities::Unreachable;

namespace {
constexpr std::uint16_t PointerBits = 32;

auto Code(PixelFormat format) -> std::uint32_t {
  switch (format) {
  case PixelFormat::Bgr24: return PIXEL_FORMAT_BGR24;
  case PixelFormat::Rgb16: return PIXEL_FORMAT_RGB16;
  default:                 Unreachable(format);
  }
}
auto Action(FrameAction action) -> std::uint32_t {
  switch (action) {
  case FrameAction::Begin: return SURFACECMD_FRAMEACTION_BEGIN;
  case FrameAction::End:   return SURFACECMD_FRAMEACTION_END;
  default:                 Unreachable(action);
  }
}
// MS-RDPBCGR 2.2.9.1.1.3.1.2.2: a bitmap's scanlines are padded to four bytes.
auto PackedStride(std::uint32_t width, std::uint32_t depth) -> std::uint32_t {
  return (width * (depth / 8) + 3) & ~3u;
}
// abi: FreeRDP types the payload fields BYTE* but only reads them (3.32 surface.c:317, update.c:339,2433-2484).
template <class ElementTy> auto Lent(std::span<ElementTy const> bytes) -> std::span<std::uint8_t> {
  auto const view = oxbox::utilities::SpanCast<std::uint8_t const>(bytes);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): the abi line above; FreeRDP never writes the payload.
  return { const_cast<std::uint8_t*>(view.data()), view.size() };
}
// Bitmap update corners are inclusive, unlike a surface command's.
auto BitmapData(Bitmap const& bitmap) -> BITMAP_DATA {
  Expects(!bitmap.payload.empty(), "bitmap payload exists");
  Expects(std::ranges::contains(std::array{ 16u, 24u, 32u }, bitmap.depth), "supported bitmap depth");
  auto const area   = bitmap.area;
  auto const stride = PackedStride(Narrowed<std::uint32_t>(area.w), bitmap.depth);
  auto       data   = BITMAP_DATA{ };
  data.destLeft           = Narrowed<std::uint32_t>(area.x);
  data.destTop            = Narrowed<std::uint32_t>(area.y);
  data.destRight          = Narrowed<std::uint32_t>(area.x + area.w - 1);
  data.destBottom         = Narrowed<std::uint32_t>(area.y + area.h - 1);
  data.width              = stride / (bitmap.depth / 8);
  data.height             = Narrowed<std::uint32_t>(area.h);
  data.bitsPerPixel       = bitmap.depth;
  data.bitmapLength       = Narrowed<std::uint32_t>(bitmap.payload.size());
  data.cbCompMainBodySize = Narrowed<std::uint32_t>(bitmap.payload.size());
  data.cbScanWidth        = stride;
  data.cbUncompressedSize = stride * data.height;
  data.bitmapDataStream   = Lent(bitmap.payload).data();
  data.compressed         = bitmap.compressed;
  return data;
}

auto LargeUpdate(PointerImage const& image) -> POINTER_LARGE_UPDATE {
  return { .xorBpp        = PointerBits,
           .cacheIndex    = 0,
           .hotSpotX      = Narrowed<std::uint16_t>(image.hot_x),
           .hotSpotY      = Narrowed<std::uint16_t>(image.hot_y),
           .width         = Narrowed<std::uint16_t>(image.size.width),
           .height        = Narrowed<std::uint16_t>(image.size.height),
           .lengthAndMask = Narrowed<std::uint32_t>(image.mask.size()),
           .lengthXorMask = Narrowed<std::uint32_t>(image.pixels.size()),
           .xorMaskData   = Lent(image.pixels).data(),
           .andMaskData   = Lent(image.mask).data() };
}
auto ColorUpdate(PointerImage const& image) -> POINTER_COLOR_UPDATE {
  auto const large = LargeUpdate(image);
  return { .cacheIndex    = large.cacheIndex,
           .hotSpotX      = large.hotSpotX,
           .hotSpotY      = large.hotSpotY,
           .width         = large.width,
           .height        = large.height,
           .lengthAndMask = Narrowed<std::uint16_t>(large.lengthAndMask),
           .lengthXorMask = Narrowed<std::uint16_t>(large.lengthXorMask),
           .xorMaskData   = large.xorMaskData,
           .andMaskData   = large.andMaskData };
}
auto PointerTable(rdpContext const& context) -> rdpPointerUpdate& {
  Expects(context.update->pointer != nullptr, "the update table carries its pointer table");
  return *context.update->pointer;
}
}
Updates::Updates(Connection& connection) noexcept : _context{ connection.Context() } { }
auto Updates::FrameMarker(FrameAction action, std::uint32_t frame) -> bool {
  auto& update = *_context.update;
  Expects(update.SurfaceFrameMarker != nullptr, "frame marker callback exists");
  SURFACE_FRAME_MARKER const marker{ Action(action), frame };
  return update.SurfaceFrameMarker(&_context, &marker);
}
auto Updates::SurfaceBits(SurfaceCommand const& command) -> bool {
  auto& update = *_context.update;
  Expects(update.SurfaceBits != nullptr, "surface callback exists");
  auto const area = command.area;
  auto       bits = SURFACE_BITS_COMMAND{ };
  bits.cmdType              = CMDTYPE_SET_SURFACE_BITS;
  bits.skipCompression      = true;
  bits.destLeft             = Narrowed<std::uint32_t>(area.x);
  bits.destTop              = Narrowed<std::uint32_t>(area.y);
  bits.destRight            = Narrowed<std::uint32_t>(area.x + area.w);
  bits.destBottom           = Narrowed<std::uint32_t>(area.y + area.h);
  bits.bmp.bpp              = 32u;
  bits.bmp.codecID          = Narrowed<std::uint16_t>(command.codec_id);
  bits.bmp.width            = Narrowed<std::uint16_t>(area.w);
  bits.bmp.height           = Narrowed<std::uint16_t>(area.h);
  bits.bmp.bitmapDataLength = Narrowed<std::uint32_t>(command.payload.size());
  bits.bmp.bitmapData       = Lent(command.payload).data();
  return update.SurfaceBits(&_context, &bits);
}
auto Updates::Bitmaps(std::span<Bitmap const> bitmaps) -> bool {
  auto& update = *_context.update;
  Expects(update.BitmapUpdate != nullptr, "bitmap callback exists");
  Expects(!bitmaps.empty(), "bitmap batch exists");
  std::vector<BITMAP_DATA> rectangles;
  rectangles.reserve(bitmaps.size());
  std::ranges::transform(bitmaps, std::back_inserter(rectangles), BitmapData);
  auto batch = BITMAP_UPDATE{ };
  batch.number          = Narrowed<std::uint32_t>(rectangles.size());
  batch.rectangles      = rectangles.data();
  batch.skipCompression = true;
  return update.BitmapUpdate(&_context, &batch);
}
auto Updates::DesktopResize() -> bool {
  auto& update = *_context.update;
  Expects(update.DesktopResize != nullptr, "desktop resize callback exists");
  return update.DesktopResize(&_context);
}
auto Updates::Pointer(PointerImage const& image) -> bool {
  auto& table = PointerTable(_context);
  Expects(table.PointerNew != nullptr, "colour pointer callback exists");
  POINTER_NEW_UPDATE const color{ PointerBits, ColorUpdate(image) };
  return table.PointerNew(&_context, &color) != 0;
}
auto Updates::LargePointer(PointerImage const& image) -> bool {
  auto& table = PointerTable(_context);
  Expects(table.PointerLarge != nullptr, "large pointer callback exists");
  auto const large = LargeUpdate(image);
  return table.PointerLarge(&_context, &large) != 0;
}
auto Updates::HidePointer() -> bool {
  auto& table = PointerTable(_context);
  Expects(table.PointerSystem != nullptr, "system pointer callback exists");
  POINTER_SYSTEM_UPDATE const hidden{ SYSPTR_NULL };
  return table.PointerSystem(&_context, &hidden) != 0;
}
auto ConvertPixels(std::span<std::uint8_t const> bgrx, Extent size, PixelFormat format) -> std::vector<std::byte> {
  auto const code   = Code(format);
  auto const stride = PackedStride(size.width, FreeRDPGetBitsPerPixel(code));
  auto const source = Stride(size.width);
  Expects(bgrx.size() >= std::size_t{ source } * size.height, "the source covers every pixel");
  std::vector<std::byte> converted(std::size_t{ stride } * size.height);
  auto const             target    = oxbox::utilities::SpanCast<std::uint8_t>(std::span{ converted });
  if (!freerdp_image_copy(target.data(), code, stride, 0, 0, size.width, size.height, bgrx.data(), PIXEL_FORMAT_BGRX32,
                          source, 0, 0, nullptr, FREERDP_FLIP_NONE))
    return { };
  return converted;
}
}
