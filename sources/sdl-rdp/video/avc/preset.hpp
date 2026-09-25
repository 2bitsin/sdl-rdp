#pragma once
#include <cstdint>

struct _NV_ENC_CONFIG;

namespace Backend::Avc {
auto ConfigurePreset(_NV_ENC_CONFIG& config, std::uint32_t bitrate, std::uint32_t fps) -> void;
} // namespace Backend::Avc
