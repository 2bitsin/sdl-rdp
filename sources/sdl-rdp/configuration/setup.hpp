#pragma once
#include <sdl-rdp/configuration/auth-mode.hpp>
#include <sdl-rdp/configuration/codec.hpp>
#include <sdl-rdp/utilities/aspect-ratio.hpp>
#include <sdl-rdp/utilities/wiped-string.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace sdl_rdp::configuration::detail::setup {
using sdl_rdp::utilities::AspectRatio;
using sdl_rdp::utilities::WipedString;

// What a backend is built from: an absent field takes the backend's default.
struct Setup {
  std::optional<std::string>           bind;
  std::uint16_t                        port            { };
  std::optional<std::filesystem::path> cert_dir;
  std::uint32_t                        width           { };
  std::uint32_t                        height          { };
  bool                                 wait_for_client { };
  bool                                 tracing         { };
  Codec                                codec           { };
  std::optional<AspectRatio>           aspect;
  std::uint32_t                        audio_latency_ms{ };
  AuthMode                             auth            { };
  std::optional<std::string>           user;
  std::optional<WipedString>           password;
  std::optional<std::string>           domain;
  std::uint32_t                        avc_bitrate_kbps{ };
};
}

namespace sdl_rdp::configuration {
using detail::setup::Setup;
}
