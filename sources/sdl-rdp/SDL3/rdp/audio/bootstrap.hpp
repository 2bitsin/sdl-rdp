#pragma once
#include <cstdint>
namespace sdl3::rdp::audio::detail::bootstrap {
auto AudioRate(std::uint32_t rate) -> void;
}
namespace sdl3::rdp::audio {
using detail::bootstrap::AudioRate;
}
