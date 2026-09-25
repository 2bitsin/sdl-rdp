#pragma once
#include "ini.hpp"
#include <sdl-rdp/abi/backend.h>
#include <filesystem>
#include <optional>
#include <string>

namespace sdl3::rdp::settings::detail::settings {
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
}
namespace sdl3::rdp::settings {
using detail::settings::Text;
using detail::settings::Settings;
}
