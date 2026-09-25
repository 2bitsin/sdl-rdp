#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/configuration/refresh.hpp>

#include <cstdint>

namespace sdl_rdp::configuration::detail::validation {
auto Validate(sdlrdp_config const& config)                   -> void;
auto ValidateCodec(sdlrdp_codec codec)                       -> void;
auto ValidRefresh(std::uint32_t mode, std::uint32_t ceiling) -> Backend::RefreshMode;
}
namespace sdl_rdp::configuration {
using detail::validation::Validate;
using detail::validation::ValidateCodec;
using detail::validation::ValidRefresh;
}
