#pragma once
#include <cstdint>
#include <ratio>
namespace sdl3::rdp::settings::detail::constants {
inline constexpr std::uint32_t MillihertzPerHertz = std::milli::den;
}

namespace sdl3::rdp::settings {
using detail::constants::MillihertzPerHertz;
}
