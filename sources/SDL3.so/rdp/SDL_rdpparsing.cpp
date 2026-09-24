#include "SDL_rdpboundary.hpp"
#include "SDL_rdpsettings.hpp"
#include <oxbox/utilities/text.hpp>
#include <algorithm>
#include <stdexcept>
namespace rdp {
namespace {
constexpr auto CodecNames = std::to_array<std::string_view>({"auto", "planar", "remotefx", "nscodec", "raw",
                                                             "progressive", "avc420"});
auto AspectPart(std::string_view text, std::string_view field) -> unsigned {
  auto const value = oxbox::utilities::WholeNumber<unsigned>(oxbox::utilities::Trimmed(text));
  if (!value || *value == 0) InvalidSetting("Invalid RDP aspect " + std::string(field) + ": " + std::string(text));
  return *value;
}
}
[[noreturn]] void InvalidSetting(std::string const& message) {
  SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "%s", message.c_str());
  throw std::runtime_error(message);
}
auto Codec(std::optional<std::string> const& text) -> sdlrdp_codec {
  if (!text) return SDLRDP_CODEC_AUTO;
  auto const* const found = std::ranges::find(CodecNames, *text);
  if (found == CodecNames.end())
    InvalidSetting("Invalid SDL_RDP_CODEC '" + *text + "'; valid names: " + oxbox::utilities::Joined(CodecNames, ", "));
  return static_cast<sdlrdp_codec>(found - CodecNames.begin());
}
auto CodecName(sdlrdp_codec codec) -> std::string {
  utilities::Expects(std::cmp_less(std::to_underlying(codec), CodecNames.size()), "codec is known");
  return std::string(CodecNames.at(static_cast<std::size_t>(codec)));
}
auto Aspect(std::optional<std::string> const& text) -> sdlrdp_aspect {
  if (!text || text->empty()) return { };
  auto const separator = text->find(':');
  if (separator == std::string::npos) InvalidSetting("Invalid RDP aspect: " + *text);
  auto const view = std::string_view{*text};
  return {AspectPart(view.substr(0, separator), "numerator"), AspectPart(view.substr(separator + 1), "denominator")};
}
}
