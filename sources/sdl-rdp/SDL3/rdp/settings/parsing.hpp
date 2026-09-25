#pragma once
#include <sdl-rdp/abi/backend.h>
#include <optional>
#include <string>
namespace sdl3::rdp::settings::detail::parsing {
[[noreturn]] auto InvalidSetting(std::string const& message)     -> void;
auto              Codec(std::optional<std::string> const& text)  -> sdlrdp_codec;
auto              CodecName(sdlrdp_codec codec)                  -> std::string;
auto              Aspect(std::optional<std::string> const& text) -> sdlrdp_aspect;
}
namespace sdl3::rdp::settings {
using detail::parsing::InvalidSetting;
using detail::parsing::Codec;
using detail::parsing::CodecName;
using detail::parsing::Aspect;
}
