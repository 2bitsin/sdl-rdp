#include <sdl-rdp-driver/SDL_rdprefresh.hpp>
#include <cstdint>
namespace rdp {
auto SameRefresh(std::uint32_t left_numerator, std::uint32_t left_denominator, std::uint32_t right_numerator,
                 std::uint32_t right_denominator) -> bool {
  auto const left  = std::uint64_t{ left_numerator } * right_denominator;
  auto const right = std::uint64_t{ right_numerator } * left_denominator;
  return left_denominator != 0 && right_denominator != 0 && left == right;
}
}
