#include <sdl-rdp/configuration/user-data.hpp>

#include <sdl-rdp/configuration/user-data.posix.hpp>

#include <filesystem>

namespace sdl_rdp::configuration::detail::user_data {
auto UserDataDirectory() -> std::filesystem::path {
  return Home() / "Library/Application Support/sdl-rdp";
}
}
