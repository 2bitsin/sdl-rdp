#include "options.hpp"
#include <sdl-rdp/SDL3/rdp/exceptions.hpp>
#include <sdl-rdp/settings/file.hpp>

#include <cstdint>
#include <filesystem>
#if defined(SDL_PLATFORM_WINDOWS)
#include "src/core/windows/SDL_windows.h"
#else
#include <dlfcn.h>
#endif

namespace sdl3::rdp::settings::detail::options {
using sdl_rdp::settings::Load;
using sdl_rdp::settings::Located;
using sdl_rdp::settings::SettingsName;
using sdl_rdp::utilities::Ensures;
auto Text(char const* text) -> std::optional<std::string> {
  if (!text) return std::nullopt;
  return std::string{ text };
}
namespace {
auto LoadedFile() -> std::filesystem::path {
#if defined(SDL_PLATFORM_WINDOWS)
  constexpr std::uint32_t long_path_limit = 32768;
  HMODULE                 module          { };
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<std::wstring::const_pointer>(LoadedFile), &module))
    throw UnlocatedLibrary{ };
  std::wstring filename(long_path_limit, L'\0');
  auto const   length   = GetModuleFileNameW(module, filename.data(), long_path_limit);
  if (!length || length >= long_path_limit) throw UnlocatedLibrary{ };
  filename.resize(length);
  return filename;
#else
  // dladdr returns borrowed loader strings for a C ABI function address.
  Dl_info info{ };
  if (!dladdr(reinterpret_cast<void*>(LoadedFile), &info) || !info.dli_fname) throw UnlocatedLibrary{ };
  return info.dli_fname;
#endif
}
auto LibraryPath() -> std::filesystem::path {
  auto path = LoadedFile();
  Ensures(!path.empty(), "the loader names the library's file");
  return path;
}
auto ExplicitPath() -> std::optional<std::filesystem::path> {
  auto path = Text(SDL_GetHintFromCode(SDL_HINT_RDP_SETTINGS));
  if (!path) path = Text(SDL_getenv(SDL_HINT_RDP_SETTINGS));
  if (path && !std::filesystem::is_regular_file(*path)) throw UnreadableSettings{ *path };
  return path;
}
auto SettingsPath() -> std::optional<std::filesystem::path> {
  if (auto path = ExplicitPath()) return path;
  auto const library = LibraryPath();
  auto const name    = SettingsName(library);
  if (auto path = Located(library.parent_path(), name)) return path;
  return Located({ }, name);
}
}
Options::Options() : _file{ SettingsPath().transform(Load).value_or(Settings{ }) } { }
}
