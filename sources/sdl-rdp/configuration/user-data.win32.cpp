#include <sdl-rdp/configuration/user-data.hpp>

#include <sdl-rdp/configuration/exceptions.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <filesystem>
#include <memory>
#include <type_traits>
#include <windows.h>

#include <knownfolders.h>
#include <shlobj.h>

namespace sdl_rdp::configuration::detail::user_data {
using sdl_rdp::utilities::Releases;

namespace {
using KnownFolder = std::unique_ptr<std::remove_pointer_t<PWSTR>, Releases<::CoTaskMemFree>>;
}
// %LOCALAPPDATA%: the machine's own per-user store, never roamed to another.
auto UserDataDirectory() -> std::filesystem::path {
  // abi: found is SHGetKnownFolderPath's out-parameter, owned by KnownFolder two lines on.
  PWSTR             found   = nullptr;
  auto const        located = ::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &found);
  KnownFolder const folder  { found };
  if (FAILED(located) || !folder) throw HomeUnavailable{ };
  return std::filesystem::path(folder.get()) / "sdl-rdp";
}
}
