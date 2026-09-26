#pragma once
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/wiped-string.hpp>

#include <_buildutil/reflect.hpp>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace sdl_rdp::configuration::detail::setup {
using sdl_rdp::utilities::AspectRatio;
using sdl_rdp::utilities::WipedString;

enum class AuthMode : std::uint8_t { None _Label("none") = 0, Tls _Label("tls") = 1, Nla _Label("nla") = 2 };
constexpr auto reflect_scheme(AuthMode* tag);
enum class Codec : std::uint8_t {
  Auto _Label("auto")               = 0,
  Planar _Label("planar")           = 1,
  RemoteFx _Label("remotefx")       = 2,
  NsCodec _Label("nscodec")         = 3,
  Raw _Label("raw")                 = 4,
  Progressive _Label("progressive") = 5,
  Avc420 _Label("avc420")           = 6
};
constexpr auto reflect_scheme(Codec* tag);
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
using detail::setup::AuthMode;
using detail::setup::Codec;
using detail::setup::Setup;
}
