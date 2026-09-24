#pragma once
#include <sdl-rdp/utilities/contract.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
namespace Backend {
template <class Byte> struct Pitched {
  std::span<Byte> bytes;
  std::size_t     pitch{ };
};
struct RowBlock {
  std::size_t rows     { };
  std::size_t row_bytes{ };
};
template <class Byte> auto CoversRows(Pitched<Byte> image, RowBlock block) -> bool {
  return block.rows == 0 || image.bytes.size() >= ((block.rows - 1) * image.pitch) + block.row_bytes;
}
inline auto CopyRows(Pitched<uint8_t const> source, Pitched<uint8_t> destination, RowBlock block, bool flip = false)
    -> void {
  utilities::Expects(source.pitch >= block.row_bytes, "pitches cover copied bytes");
  utilities::Expects(destination.pitch >= block.row_bytes, "pitches cover copied bytes");
  utilities::Expects(CoversRows(source, block), "source covers rows");
  utilities::Expects(CoversRows(destination, block), "destination covers rows");
  for (auto row : std::views::iota(std::size_t{ 0 }, block.rows))
    std::ranges::copy(source.bytes.subspan(row * source.pitch, block.row_bytes),
                      destination.bytes.subspan((flip ? block.rows - row - 1 : row) * destination.pitch).begin());
}
}
