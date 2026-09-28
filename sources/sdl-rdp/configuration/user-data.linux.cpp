#include <sdl-rdp/configuration/user-data.hpp>

#include <sdl-rdp/configuration/user-data.posix.hpp>

#include <cstdlib>
#include <filesystem>

namespace sdl_rdp::configuration::detail::user_data {
// XDG Base Directory Specification: $XDG_DATA_HOME, else ~/.local/share.
auto UserDataDirectory() -> std::filesystem::path {
  if (auto const* data = std::getenv("XDG_DATA_HOME"); data && *data) return std::filesystem::path(data) / "sdl-rdp";
  return Home() / ".local/share/sdl-rdp";
}
}
