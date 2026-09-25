#pragma once
#include <sdl-rdp/SDL3/rdp/exceptions.hpp>
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/utilities/contained.hpp>
#include <optional>
#include <string>
#include <string_view>
namespace sdl3::rdp::settings::detail::parsing {
auto ReportInvalidSetting(std::string_view text)    -> void;
auto Codec(std::optional<std::string> const& text)  -> sdlrdp_codec;
auto CodecName(sdlrdp_codec codec)                  -> std::string;
auto Aspect(std::optional<std::string> const& text) -> sdlrdp_aspect;
// A setting the user wrote wrongly is logged where the user looks, then fails the call that read it.
template <typename FailureTy, typename... ArgsTy>
[[noreturn]] auto InvalidSetting(ArgsTy const&... args) -> void {
  throw ::Backend::Reported(FailureTy{ args... }, ReportInvalidSetting);
}
}
namespace sdl3::rdp::settings {
using detail::parsing::ReportInvalidSetting;
using detail::parsing::InvalidSetting;
using detail::parsing::Codec;
using detail::parsing::CodecName;
using detail::parsing::Aspect;
}
