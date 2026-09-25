#include "parsing.hpp"
#include <sdl-rdp/SDL3/rdp/backend/sdl-internals.hpp>
#include <string>
#include <string_view>
namespace sdl3::rdp::settings::detail::parsing {
auto ReportInvalidSetting(std::string_view text) -> void {
  SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "%s", std::string{ text }.c_str());
}
auto FromText([[maybe_unused]] std::type_identity<std::string> type, [[maybe_unused]] std::string_view name,
              std::string const& text) -> std::string {
  return text;
}
auto FromText([[maybe_unused]] std::type_identity<bool> type, [[maybe_unused]] std::string_view name,
              std::string const& text) -> bool {
  return SDL_GetStringBoolean(text.c_str(), false);
}
}
