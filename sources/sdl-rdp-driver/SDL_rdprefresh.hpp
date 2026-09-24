#pragma once
#include <cstdint>
namespace rdp {
auto SameRefresh(std::uint32_t left_numerator, std::uint32_t left_denominator, std::uint32_t right_numerator,
                 std::uint32_t right_denominator) -> bool;
}
