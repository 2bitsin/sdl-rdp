#include "settings.hpp"
#include "parsing.hpp"
#include <oxbox/utilities/number-text.hpp>
#include <oxbox/utilities/text.hpp>
#include <sdl-rdp/SDL3/rdp/backend/resources.hpp>
#include <sdl-rdp/SDL3/rdp/exceptions.hpp>
#include <cstdint>
#if defined(SDL_PLATFORM_WINDOWS)
#include "src/core/windows/SDL_windows.h"
#else
#include <dlfcn.h>
#endif

namespace sdl3::rdp::settings::detail::settings {
using backend::LoadedFile;
auto Text(char const* text) -> std::optional<std::string> {
  if (!text) return std::nullopt;
  return std::string{ text };
}
namespace {
constexpr auto IniFileName = "libSDL3.ini";
auto LibraryPath() -> std::filesystem::path {
#if defined(SDL_PLATFORM_WINDOWS)
  constexpr std::uint32_t long_path_limit = 32768;
  HMODULE                 module          { };
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<std::wstring::const_pointer>(LibraryPath), &module))
    return { };
  std::wstring filename(long_path_limit, L'\0');
  auto const   length   = GetModuleFileNameW(module, filename.data(), long_path_limit);
  if (!length || length >= long_path_limit) return { };
  filename.resize(length);
  return filename;
#else
  // dladdr returns borrowed loader strings for a C ABI function address.
  Dl_info info{ };
  if (!dladdr(reinterpret_cast<void*>(LibraryPath), &info) || !info.dli_fname) return { };
  return info.dli_fname;
#endif
}
auto LibraryIni() -> std::optional<std::filesystem::path> {
  auto const library = LibraryPath();
  if (!library.has_parent_path()) return std::nullopt;
  return library.parent_path() / IniFileName;
}
auto WarnIniEntry(std::filesystem::path const& path, IniEntry const& entry) -> void {
  auto const file = path.string();
  if (entry.Status() == IniStatus::MALFORMED)
    SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "%s:%zu: malformed ini line (missing '=')", file.c_str(), entry.Line());
  else
    SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "%s:%zu: unknown RDP setting '%.*s'", file.c_str(), entry.Line(),
                static_cast<int>(entry.Key().size()), entry.Key().data());
}
}
auto Settings::_Read(std::filesystem::path const& path) -> bool {
  utilities::Expects(!path.empty(), "ini path is specified");
  SDL_PathInfo info{ };
  if (!SDL_GetPathInfo(path.string().c_str(), &info)) {
    SDL_ClearError();
    return false;
  }
  LoadedFile const text{ path };
  ParseIni(text.Get(), [&](IniEntry const& entry) {
    if (auto const index = entry.Index())
      _values.at(*index) = entry.Value();
    else
      WarnIniEntry(path, entry);
  });
  return true;
}
Settings::Settings() {
  auto explicit_path = Text(SDL_GetHintFromCode(SDL_HINT_RDP_INI));
  if (!explicit_path) explicit_path = Text(SDL_getenv(SDL_HINT_RDP_INI));
  if (explicit_path) {
    if (explicit_path->empty() || !_Read(*explicit_path)) throw UnreadableIni{ *explicit_path };
    return;
  }
  auto const beside_library = LibraryIni();
  if (!beside_library || !_Read(*beside_library)) _Read(IniFileName);
}
auto Settings::_Fallback(std::string const& name) const -> std::optional<std::string> {
  if (auto value = IniValue(_values, name)) return value;
  return Text(SDL_getenv(name.c_str()));
}
auto Settings::Get(std::string const& name) const -> std::optional<std::string> {
  utilities::Expects(!name.empty(), "setting has a name");
  if (auto hint = Text(SDL_GetHintFromCode(name.c_str()))) return hint;
  return _Fallback(name);
}
auto Settings::Changed(std::string const& name, std::optional<std::string> const& old_value,
                       std::optional<std::string> const& new_value) const -> std::optional<std::string> {
  utilities::Expects(!name.empty(), "changed hint has a name");
  auto const code = Text(SDL_GetHintFromCode(name.c_str()));
  // SDL_ResetHint calls observers before removing the old code value.
  return old_value != new_value && code && code != new_value ? _Fallback(name) : Get(name);
}
auto Settings::Integer(std::string const& name, int fallback, int minimum, int maximum) const -> int {
  utilities::Expects(minimum <= maximum, "integer setting range is ordered");
  auto const text = Get(name);
  if (!text) return fallback;
  auto const number = oxbox::utilities::ParseNumber<int>(oxbox::utilities::Trimmed(*text));
  if (!number || *number < minimum || *number > maximum)
    InvalidSetting<IntegerOutOfRange>(name, *text, minimum, maximum);
  return *number;
}
auto Settings::Boolean(std::string const& name, bool fallback) const -> bool {
  auto const text = Get(name);
  return text ? SDL_GetStringBoolean(text->c_str(), fallback) : fallback;
}
}
