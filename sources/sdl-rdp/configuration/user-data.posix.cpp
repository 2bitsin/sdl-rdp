#include <sdl-rdp/configuration/user-data.posix.hpp>

#include <sdl-rdp/configuration/exceptions.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <pwd.h>
#include <unistd.h>

namespace sdl_rdp::configuration::detail::user_data {
auto Home() -> std::filesystem::path {
  if (auto const* home = std::getenv("HOME"); home && *home) return home;
  std::array<char, 16384> buffer { };
  passwd                  entry  { };
  passwd*                 found  = nullptr;
  if (getpwuid_r(getuid(), &entry, buffer.data(), buffer.size(), &found) || !found) throw HomeUnavailable{ };
  return entry.pw_dir;
}
}
