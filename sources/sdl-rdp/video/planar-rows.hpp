#pragma once
#include <sdl-rdp/picture/frame-snapshot.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/rect.hpp>
#include <sdl-rdp/video/encoder.hpp>
#include <sdl-rdp/video/scaler.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sdl_rdp::video::detail::planar_rows {
using sdl_rdp::utilities::RowBytes;
using sdl_rdp::utilities::Rows;

template <std::predicate<sdlrdp_rect, std::span<std::byte const>> Consume>
auto EncodePlanarRows(Encoder& encoder, Scaler& scaler, sdlrdp_rect area, Consume consume) -> bool {
  std::vector<std::uint8_t> scratch(RowBytes(area.w));
  return std::ranges::all_of(Rows(area), [&](sdlrdp_rect row) {
    auto const pixels = scaler.Copy(row, scratch, RowOrder::TopDown);
    return encoder.Encode(pixels.Pixels(), row.w, 1) && consume(row, encoder.Payload());
  });
}
}
