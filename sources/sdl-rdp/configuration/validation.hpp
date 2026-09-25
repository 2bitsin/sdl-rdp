#pragma once
#include <sdl-rdp/configuration/codec.hpp>
#include <sdl-rdp/configuration/refresh.hpp>
#include <sdl-rdp/configuration/setup.hpp>

#include <cstdint>

namespace sdl_rdp::configuration::detail::validation {
using sdl_rdp::configuration::Codec;
using sdl_rdp::configuration::RefreshMode;
using sdl_rdp::configuration::Setup;
auto Validate(Setup const& config)                            -> void;
auto ValidateCodec(Codec codec)                               -> void;
auto ValidateRefresh(RefreshMode mode, std::uint32_t ceiling) -> void;
}

namespace sdl_rdp::configuration {
using detail::validation::Validate;
using detail::validation::ValidateCodec;
using detail::validation::ValidateRefresh;
}
