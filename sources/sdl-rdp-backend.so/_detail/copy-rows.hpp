#pragma once
#include "contract.hpp"
#include <winpr/wtypes.h>
#include <span>
#include <algorithm>
#include <ranges>
namespace Backend {
inline void CopyRows(std::span<BYTE const> source, std::size_t source_pitch,
                     std::span<BYTE> destination, std::size_t destination_pitch,
                     std::size_t rows, std::size_t bytes_per_row)
{
  utilities::Expects(source_pitch >= bytes_per_row && destination_pitch >= bytes_per_row,
                     "pitches cover copied bytes");
  utilities::Expects(!rows || (source.size() >= (rows - 1) * source_pitch + bytes_per_row
    && destination.size() >= (rows - 1) * destination_pitch + bytes_per_row), "spans cover rows");
  for (auto row : std::views::iota(std::size_t{0}, rows))
    std::ranges::copy(source.subspan(row * source_pitch, bytes_per_row),
                      destination.subspan(row * destination_pitch).begin());
}
}
