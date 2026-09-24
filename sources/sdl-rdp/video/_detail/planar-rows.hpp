#pragma once
#include <sdl-rdp/core/frame-snapshot.hpp>
#include <sdl-rdp/utilities/rect.hpp>
#include <sdl-rdp/video/encoder.hpp>
#include <sdl-rdp/video/scaler.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <span>

namespace Backend {
template <std::predicate<sdlrdp_rect, std::span<BYTE const>> Consume>
auto EncodePlanarRows(Encoder& encoder, Scaler& scaler, sdlrdp_rect area, Consume consume) -> bool {
  auto const scratch = encoder.Scratch(std::size_t(area.w) * PixelBytes);
  return std::ranges::all_of(Rows(area), [&](sdlrdp_rect row) {
    auto const pixels = scaler.Copy(row, scratch, RowOrder::TopDown);
    return encoder.Encode(pixels.Pixels(), row.w, 1) && consume(row, encoder.Payload());
  });
}
} // namespace Backend
