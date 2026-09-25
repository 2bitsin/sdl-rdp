#include <sdl-rdp/settings/aspect.hpp>
#include <sdl-rdp/settings/parsed.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <oxbox/utilities/number-text.hpp>
#include <oxbox/utilities/text.hpp>
#include <algorithm>
#include <format>

namespace sdl_rdp::settings::detail::aspect {
using sdl_rdp::utilities::Expects;

Aspect::Aspect(std::uint32_t numerator, std::uint32_t denominator) : _parts{ Parts{ numerator, denominator } } {
  Expects(numerator > 0, "an aspect numerator is positive");
  Expects(denominator > 0, "an aspect denominator is positive");
}
auto Aspect::None() -> Aspect {
  return Aspect{ };
}
auto Aspect::Form() -> std::string_view {
  return "an aspect of two positive whole numbers as N:D";
}
auto Aspect::Parsed(std::string_view text) -> std::optional<Aspect> {
  auto const trimmed = oxbox::utilities::Trimmed(text);
  if (trimmed.empty()) return None();
  auto const parts = oxbox::utilities::ParseNumbers<std::uint32_t, 2>(trimmed, ':');
  if (!parts || std::ranges::contains(*parts, 0U)) return std::nullopt;
  return Aspect{ (*parts)[0], (*parts)[1] };
}
auto Aspect::_Decode(std::string const& text) -> Aspect {
  return ParsedOrRefused<Aspect>(text);
}
auto Aspect::_Encode() const -> std::string {
  return Text();
}
auto Aspect::Text() const -> std::string {
  return _parts.transform([](Parts const& parts) { return std::format("{}:{}", parts.first, parts.second); })
      .value_or("");
}
auto Aspect::IsNone() const -> bool {
  return !_parts.has_value();
}
auto Aspect::Ratio() const -> sdlrdp_aspect {
  Expects(!IsNone(), "only a stated aspect has a ratio");
  auto const parts = _parts.value_or(Parts{ });
  return { parts.first, parts.second };
}
}
