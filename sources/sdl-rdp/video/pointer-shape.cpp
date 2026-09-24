#include <sdl-rdp/video/pointer-shape.hpp>

#include <sdl-rdp/core/frame-snapshot.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/freerdp.h>
#include <freerdp/settings.h>
#include <freerdp/update.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <vector>

namespace Backend {
namespace {
constexpr std::uint32_t ColorPointerLimit = 96;
constexpr uint16_t      ColorBits         = 32;
constexpr std::uint8_t  TransparentAlpha  = 0;
auto MaskStride(std::uint32_t width) -> std::size_t {
  return std::size_t((width + 15) / 16) * 2;
}
auto MarkTransparent(std::span<std::uint8_t const> source, std::span<std::uint8_t> mask_row) -> void {
  constexpr std::size_t AlphaByte = 3;
  for (auto column : std::views::iota(0uz, source.size() / PixelBytes))
    if (source[(column * PixelBytes) + AlphaByte] == TransparentAlpha) mask_row[column / 8] |= 0x80 >> (column % 8);
}
// abi: FreeRDP's pointer update structs hold the buffers as non-const pointers and only read them.
auto MutableForAbi(std::vector<std::uint8_t> const& bytes) -> std::uint8_t* {
  return const_cast<std::uint8_t*>(bytes.data());
}
auto Delivered(bool sent) -> PointerDelivery {
  return sent ? PointerDelivery::Sent : PointerDelivery::Failed;
}
}
PointerShape::PointerShape(Extent size, std::uint32_t x, std::uint32_t y, std::span<std::uint8_t const> argb)
    : _size{ size }, _hot_x{ x }, _hot_y{ y }, _pixels(std::size_t(size.width) * size.height * PixelBytes),
      _mask(MaskStride(size.width) * size.height) {
  Expects(size.width <= LargePointerLimit, "pointer width fits a large pointer");
  Expects(size.height <= LargePointerLimit, "pointer height fits a large pointer");
  Expects(argb.size() >= _pixels.size(), "source covers every pointer pixel");
  auto const stride = MaskStride(size.width);
  auto const row    = std::size_t(size.width) * PixelBytes;
  for (auto line : std::views::iota(0u, size.height)) {
    auto const source = argb.subspan(std::size_t(line) * row, row);
    auto const target = std::size_t(size.height - line - 1);
    std::ranges::copy(source, _pixels.begin() + std::ptrdiff_t(target * row));
    MarkTransparent(source, std::span(_mask).subspan(target * stride, stride));
  }
}
auto PointerShape::ColorImage() const -> POINTER_COLOR_UPDATE {
  auto const large = LargeImage();
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
auto PointerShape::LargeImage() const -> POINTER_LARGE_UPDATE {
  return { .xorBpp        = ColorBits,
           .cacheIndex    = 0,
           .hotSpotX      = Narrowed<std::uint16_t>(_hot_x),
           .hotSpotY      = Narrowed<std::uint16_t>(_hot_y),
           .width         = Narrowed<std::uint16_t>(_size.width),
           .height        = Narrowed<std::uint16_t>(_size.height),
           .lengthAndMask = Narrowed<std::uint32_t>(_mask.size()),
           .lengthXorMask = Narrowed<std::uint32_t>(_pixels.size()),
           .xorMaskData   = MutableForAbi(_pixels),
           .andMaskData   = MutableForAbi(_mask) };
}
auto PointerShape::Send(rdpContext& context) const -> PointerDelivery {
  auto* update = context.update->pointer;
  if (!_size.width) {
    POINTER_SYSTEM_UPDATE const hidden{ SYSPTR_NULL };
    return Delivered(update->PointerSystem(&context, &hidden) != 0);
  }
  if (_size.width <= ColorPointerLimit && _size.height <= ColorPointerLimit) {
    POINTER_NEW_UPDATE const image{ ColorBits, ColorImage() };
    return Delivered(update->PointerNew(&context, &image) != 0);
  }
  if (!(freerdp_settings_get_uint32(context.settings, FreeRDP_LargePointerFlag) & LARGE_POINTER_FLAG_384x384))
    return PointerDelivery::Unsupported;
  return Delivered(SendLarge(context));
}
auto PointerShape::SendLarge(rdpContext& context) const -> bool {
  auto const image = LargeImage();
  return context.update->pointer->PointerLarge(&context, &image) != 0;
}
}
