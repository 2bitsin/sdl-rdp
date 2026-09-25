#include "parsing.hpp"
#include <oxbox/utilities/number-text.hpp>
#include <oxbox/utilities/text.hpp>
#include <sdl-rdp/SDL3/rdp/backend/sdl-internals.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
namespace sdl3::rdp::settings::detail::parsing {
namespace {
constexpr auto CodecNames = std::to_array<std::string_view>(
    { "auto", "planar", "remotefx", "nscodec", "raw", "progressive", "avc420" });
}
auto ReportInvalidSetting(std::string_view text) -> void {
  SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "%s", std::string{ text }.c_str());
}
auto Codec(std::optional<std::string> const& text) -> sdlrdp_codec {
  if (!text) return SDLRDP_CODEC_AUTO;
  auto const* const found = std::ranges::find(CodecNames, *text);
  if (found == CodecNames.end())
    InvalidSetting<UnknownName>("SDL_RDP_CODEC", *text, oxbox::utilities::Joined(CodecNames, ", "));
  return static_cast<sdlrdp_codec>(found - CodecNames.begin());
}
auto CodecName(sdlrdp_codec codec) -> std::string {
  utilities::Expects(std::cmp_less(std::to_underlying(codec), CodecNames.size()), "codec is known");
  return std::string(CodecNames.at(static_cast<std::size_t>(codec)));
}
auto Aspect(std::optional<std::string> const& text) -> sdlrdp_aspect {
  if (!text || text->empty()) return { };
  auto const parts = oxbox::utilities::ParseNumbers<std::uint32_t, 2>(oxbox::utilities::Trimmed(*text), ':');
  if (!parts || std::ranges::contains(*parts, 0U)) InvalidSetting<InvalidAspect>(*text);
  return { (*parts)[0], (*parts)[1] };
}
}
