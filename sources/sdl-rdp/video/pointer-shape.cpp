#include <sdl-rdp/video/pointer-shape.hpp>

#include <sdl-rdp/core/frame-snapshot.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <freerdp/freerdp.h>
#include <freerdp/settings.h>
#include <freerdp/update.h>
#include <algorithm>
#include <cstddef>
#include <ranges>
#include <span>

namespace Backend {
namespace {
constexpr unsigned ColorPointerLimit = 96;
constexpr BYTE     TransparentAlpha  = 0;
auto MaskStride(unsigned width) -> std::size_t {
  return std::size_t((width + 15) / 16) * 2;
}
auto MarkTransparent(std::span<BYTE const> source, std::span<BYTE> mask_row) -> void {
  constexpr std::size_t AlphaByte = 3;
  for (auto column : std::views::iota(0uz, source.size() / PixelBytes))
    if (source[(column * PixelBytes) + AlphaByte] == TransparentAlpha) mask_row[column / 8] |= 0x80 >> (column % 8);
}
auto Delivered(BOOL sent) -> PointerDelivery {
  return sent ? PointerDelivery::Sent : PointerDelivery::Failed;
}
}
PointerShape::PointerShape(Extent size, unsigned x, unsigned y, std::span<BYTE const> argb)
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
auto PointerShape::Send(rdpContext& context) -> PointerDelivery {
  auto* update = context.update->pointer;
  if (!_size.width) {
    POINTER_SYSTEM_UPDATE const hidden{ SYSPTR_NULL };
    return Delivered(update->PointerSystem(&context, &hidden));
  }
  if (_size.width <= ColorPointerLimit && _size.height <= ColorPointerLimit) {
    POINTER_NEW_UPDATE const image{ 32,
                                    { 0, UINT16(_hot_x), UINT16(_hot_y), UINT16(_size.width), UINT16(_size.height),
                                      UINT16(_mask.size()), UINT16(_pixels.size()), _pixels.data(), _mask.data() } };
    return Delivered(update->PointerNew(&context, &image));
  }
  if (!(freerdp_settings_get_uint32(context.settings, FreeRDP_LargePointerFlag) & LARGE_POINTER_FLAG_384x384))
    return PointerDelivery::Unsupported;
  return Delivered(SendLarge(context));
}
auto PointerShape::SendLarge(rdpContext& context) -> BOOL {
  POINTER_LARGE_UPDATE const image{ 32,
                                    0,
                                    UINT16(_hot_x),
                                    UINT16(_hot_y),
                                    UINT16(_size.width),
                                    UINT16(_size.height),
                                    UINT32(_mask.size()),
                                    UINT32(_pixels.size()),
                                    _pixels.data(),
                                    _mask.data() };
  return context.update->pointer->PointerLarge(&context, &image);
}
}
