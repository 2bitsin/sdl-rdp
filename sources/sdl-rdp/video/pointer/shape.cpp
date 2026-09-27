#include <sdl-rdp/video/pointer/shape.hpp>

#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/freerdp-facade/updates.hpp>
#include <sdl-rdp/picture/frame-snapshot.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <vector>

namespace sdl_rdp::video::pointer::detail::shape {
using sdl_rdp::freerdp_facade::Updates;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::PixelBytes;

namespace {
constexpr std::uint32_t ColorPointerLimit = 96;
constexpr std::uint8_t  TransparentAlpha  = 0;
auto MaskStride(std::uint32_t width) -> std::size_t {
  return std::size_t{ (width + 15) / 16 } * 2;
}
auto MarkTransparent(std::span<std::uint8_t const> source, std::span<std::uint8_t> mask_row) -> void {
  constexpr std::size_t AlphaByte = 3;
  for (auto column : std::views::iota(0uz, source.size() / PixelBytes))
    if (source[(column * PixelBytes) + AlphaByte] == TransparentAlpha) mask_row[column / 8] |= 0x80 >> (column % 8);
}
auto Delivered(bool sent) -> PointerDelivery {
  return sent ? PointerDelivery::Sent : PointerDelivery::Failed;
}
}
PointerShape::PointerShape(PointerLayout const& layout, std::span<std::uint8_t const> argb)
    : _size{ layout.Size() }, _hot_x{ layout.X() }, _hot_y{ layout.Y() }, _pixels(layout.Bytes()),
      _mask(MaskStride(_size.width) * _size.height) {
  Expects(argb.size() >= _pixels.size(), "source covers every pointer pixel");
  auto const stride = MaskStride(_size.width);
  auto const row    = std::size_t{ _size.width } * PixelBytes;
  for (auto line : std::views::iota(0u, _size.height)) {
    auto const source = argb.subspan(std::size_t{ line } * row, row);
    auto const target = std::size_t{ _size.height - line - 1 };
    std::ranges::copy(source, _pixels.begin() + Narrowed<std::ptrdiff_t>(target * row));
    MarkTransparent(source, std::span(_mask).subspan(target * stride, stride));
  }
}
auto PointerShape::Image() const noexcept -> PointerImage {
  return { .size = _size, .hot_x = _hot_x, .hot_y = _hot_y, .pixels = _pixels, .mask = _mask };
}
auto PointerShape::Send(Connection& connection) const -> PointerDelivery {
  Updates updates{ connection };
  if (!_size.width) return Delivered(updates.HidePointer());
  if (_size.width <= ColorPointerLimit && _size.height <= ColorPointerLimit) return Delivered(updates.Pointer(Image()));
  if (!connection.Settings().LargePointer().up_to_384x384) return PointerDelivery::Unsupported;
  return Delivered(updates.LargePointer(Image()));
}
}
