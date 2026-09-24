#include "SDL_rdpini.hpp"
#include "_detail/contract.hpp"
#include <algorithm>

namespace rdp {
namespace {
auto Unquoted(std::string_view value) -> std::string_view {
  auto const quoted = value.size() >= 2 && value.starts_with('"') && value.ends_with('"');
  return quoted ? value.substr(1, value.size() - 2) : value;
}
}
auto SettingIndex(std::string_view name) -> std::optional<std::size_t> {
  auto const* const found = std::ranges::find(SettingNames, name);
  if (found == SettingNames.end()) return std::nullopt;
  return static_cast<std::size_t>(found - SettingNames.begin());
}
auto IniValue(SettingValues const& values, std::string_view name) -> std::optional<std::string> {
  auto const index = SettingIndex(name);
  return index ? values.at(*index) : std::nullopt;
}
auto IsIniEntry(std::string_view line) -> bool {
  auto const comment = line.starts_with('#') || line.starts_with(';');
  auto const section = line.starts_with('[') && line.ends_with(']');
  return !line.empty() && !comment && !section;
}
auto ParseEntry(std::string_view line, unsigned number) -> IniEntry {
  utilities::Expects(number > 0, "ini lines are one based");
  auto const equals = line.find('=');
  if (equals == std::string_view::npos) return IniEntry{number};
  return {oxbox::utilities::Trimmed(line.substr(0, equals)),
          Unquoted(oxbox::utilities::Trimmed(line.substr(equals + 1))), number};
}
IniEntry::IniEntry(std::string_view key, std::string_view value, unsigned line)
    : _index{SettingIndex(key)}, _key{key}, _value{value}, _line{line},
      _status{ _index ? IniStatus::SETTING : IniStatus::UNKNOWN } { }
IniEntry::IniEntry(unsigned line) : _line{ line }, _status{ IniStatus::MALFORMED } { }
auto IniEntry::Index() const -> std::optional<std::size_t> { return _index; }
auto IniEntry::Key() const -> std::string_view { return _key; }
auto IniEntry::Value() const -> std::string_view { return _value; }
auto IniEntry::Line() const -> unsigned { return _line; }
auto IniEntry::Status() const -> IniStatus { return _status; }
}
