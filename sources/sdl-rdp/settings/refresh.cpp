#include <sdl-rdp/settings/refresh.hpp>
#include <sdl-rdp/settings/parsed.hpp>
#include <sdl-rdp/settings/settings.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <oxbox/serialization/io.hpp>
#include <oxbox/utilities/number-text.hpp>
#include <oxbox/utilities/text.hpp>
#include <format>

namespace sdl_rdp::settings::detail::refresh {
using sdl_rdp::utilities::Expects;

namespace {
auto AutomaticMode(std::string_view text) -> std::optional<RefreshMode> {
  auto const mode = oxbox::serialization::FromString<RefreshMode>(text);
  return mode == RefreshMode::Fixed ? std::nullopt : mode;
}
auto FixedRate(std::string_view text) -> std::optional<Refresh> {
  auto const number = oxbox::utilities::ParseNumber<std::int64_t>(text);
  if (!number || !Refresh::Rate::Admits(*number)) return std::nullopt;
  return Refresh{ Refresh::Rate{ static_cast<std::uint32_t>(*number) } };
}
}
Refresh::Refresh(Rate rate) : _rate{ rate } { }
Refresh::Refresh(RefreshMode mode) : _mode{ mode } {
  Expects(mode != RefreshMode::Fixed, "a fixed refresh names its rate");
}
auto Refresh::Form() -> std::string_view {
  return "a refresh: auto-client, auto-client-average, auto-sender or a whole number of hertz";
}
auto Refresh::Parsed(std::string_view text) -> std::optional<Refresh> {
  auto const trimmed = oxbox::utilities::Trimmed(text);
  if (auto const mode = AutomaticMode(trimmed)) return Refresh{ *mode };
  return FixedRate(trimmed);
}
auto Refresh::_Decode(std::string const& text) -> Refresh {
  return ParsedOrRefused<Refresh>(text);
}
auto Refresh::_Encode() const -> std::string {
  if (_mode == RefreshMode::Fixed) return std::format("{}", _rate.Get());
  return std::string{ NameOf(_mode) };
}
auto Refresh::Mode() const -> RefreshMode {
  return _mode;
}
auto Refresh::Hz() const -> std::uint32_t {
  return _rate.Get();
}
}
