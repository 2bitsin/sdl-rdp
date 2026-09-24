#pragma once
#include "SDL_rdpini.hpp"
#include "sdl-rdp-backend.so/sdl-rdp-backend.h"
#include <filesystem>
#include <optional>
#include <string>

namespace rdp {
// SDL hint and environment APIs return nullable borrowed C strings.
auto Text(char const* text) -> std::optional<std::string>;
class Settings {
public:
       Settings();
  auto Get(std::string const& name) const                                             -> std::optional<std::string>;
  auto Changed(std::string const& name, std::optional<std::string> const& old_value,
               std::optional<std::string> const& new_value) const -> std::optional<std::string>;
  auto Integer(std::string const& name, int fallback, int minimum, int maximum) const -> int;
  auto Boolean(std::string const& name, bool fallback) const                          -> bool;
private:
  auto _Fallback(std::string const& name) const -> std::optional<std::string>;
  auto _Read(std::filesystem::path const& path) -> bool;
  SettingValues _values{ };
};
[[noreturn]] auto InvalidSetting(std::string const& message)     -> void;
auto              Codec(std::optional<std::string> const& text)  -> sdlrdp_codec;
auto              CodecName(sdlrdp_codec codec)                  -> std::string;
auto              Aspect(std::optional<std::string> const& text) -> sdlrdp_aspect;
}
