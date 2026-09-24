#pragma once
#include <array>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <oxbox/utilities/text.hpp>
namespace rdp {
inline constexpr auto SettingNames = std::to_array<std::string_view>({
    "SDL_RDP_INI", "SDL_RDP_BACKEND", "SDL_RDP_BIND", "SDL_RDP_CERT_DIR", "SDL_RDP_CODEC",
    "SDL_RDP_AUDIO_LATENCY", "SDL_RDP_AUDIO_LEAD", "SDL_RDP_VSYNC", "SDL_RDP_ASPECT", "SDL_RDP_HEIGHT",
    "SDL_RDP_PORT", "SDL_RDP_WAIT_FOR_CLIENT", "SDL_RDP_WIDTH", "SDL_RDP_REFRESH", "SDL_RDP_USER",
    "SDL_RDP_PASSWORD", "SDL_RDP_DOMAIN", "SDL_RDP_AUTH"});
using SettingValues = std::array<std::optional<std::string>, SettingNames.size()>;
enum class IniStatus{ SETTING, UNKNOWN, MALFORMED };
class IniEntry {
public:
           IniEntry(std::string_view key, std::string_view value, unsigned line);
  explicit IniEntry(unsigned line);
  auto     Index() const  -> std::optional<std::size_t>;
  auto     Key() const    -> std::string_view;
  auto     Value() const  -> std::string_view;
  auto     Line() const   -> unsigned;
  auto     Status() const -> IniStatus;
private:
  std::optional<std::size_t> _index;
  std::string_view           _key;
  std::string_view           _value;
  unsigned                   _line;
  IniStatus                  _status;
};
auto SettingIndex(std::string_view name)                          -> std::optional<std::size_t>;
auto IniValue(SettingValues const& values, std::string_view name) -> std::optional<std::string>;
auto ParseEntry(std::string_view line, unsigned number)           -> IniEntry;
auto IsIniEntry(std::string_view line)                            -> bool;
template<typename _Accept> requires std::invocable<_Accept const&, IniEntry>
auto ParseIni(std::string_view text, _Accept const& accept) -> void {
  for (auto const [index, part] : text | std::views::split('\n') | std::views::enumerate) {
    auto const line = oxbox::utilities::Trimmed(std::string_view{part});
    if (IsIniEntry(line)) std::invoke(accept, ParseEntry(line, static_cast<unsigned>(index) + 1));
  }
}
}
