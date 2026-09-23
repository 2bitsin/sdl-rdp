#pragma once
#include "contract.hpp"

#include <algorithm>
#include <ranges>
#include <span>
#include <winpr/wtypes.h>
namespace Backend {
inline void CopyRows(std::span<BYTE const> source, std::size_t source_pitch, std::span<BYTE> destination,
                     std::size_t destination_pitch, std::size_t rows, std::size_t bytes_per_row, bool flip = false) {
  utilities::Expects(source_pitch >= bytes_per_row, "pitches cover copied bytes");
  utilities::Expects(destination_pitch >= bytes_per_row, "pitches cover copied bytes");
  if (rows) utilities::Expects(source.size() >= (rows - 1) * source_pitch + bytes_per_row, "source covers rows");
  if (rows)
    utilities::Expects(destination.size() >= (rows - 1) * destination_pitch + bytes_per_row, "destination covers rows");
  for (auto row : std::views::iota(std::size_t{ 0 }, rows))
    std::ranges::copy(source.subspan(row * source_pitch, bytes_per_row),
                      destination.subspan((flip ? rows - row - 1 : row) * destination_pitch).begin());
}
}
