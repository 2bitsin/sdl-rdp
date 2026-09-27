#pragma once
#include <ffnvcodec/nvEncodeAPI.h>

#include <cstdint>

namespace sdl_rdp::video::avc::detail::preset {
auto ConfigurePreset(NV_ENC_CONFIG& config, std::uint32_t bitrate, std::uint32_t fps) -> void;
}

namespace sdl_rdp::video::avc {
using detail::preset::ConfigurePreset;
}
