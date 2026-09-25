#pragma once
#include <cstdint>
#include <ratio>
namespace sdl3::rdp::settings::detail::constants {
inline constexpr int           DefaultWidth       = 1024;
inline constexpr int           DefaultHeight      = 768;
inline constexpr std::uint32_t MillihertzPerHertz = std::milli::den;
}
namespace sdl3::rdp::settings {
using detail::constants::DefaultWidth;
using detail::constants::DefaultHeight;
using detail::constants::MillihertzPerHertz;
}
