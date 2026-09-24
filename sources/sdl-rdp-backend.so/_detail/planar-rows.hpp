#pragma once
#include "encoder.hpp"
#include "frame-snapshot.hpp"
#include "rect.hpp"
#include "scaler.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <span>

namespace Backend {
template <std::predicate<sdlrdp_rect, std::span<BYTE const>> Consume>
bool EncodePlanarRows(Encoder& encoder, Scaler& scaler, sdlrdp_rect area, Consume consume) {
  auto const scratch = encoder.Scratch(std::size_t(area.w) * PixelBytes);
  return std::ranges::all_of(Rows(area), [&](sdlrdp_rect row) {
    auto const pixels = scaler.Copy(row, scratch, RowOrder::TopDown);
    return encoder.Encode(pixels.Pixels(), row.w, 1) && consume(row, encoder.Payload());
  });
}
} // namespace Backend
