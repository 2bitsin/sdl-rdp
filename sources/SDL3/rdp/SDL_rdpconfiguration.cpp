#include "SDL_rdpconfiguration.hpp"
#include "SDL_rdpconstants.hpp"
#include <oxbox/utilities/hash.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
namespace rdp {
using namespace oxbox::utilities::literals;
namespace {
constexpr int DefaultPort           = 3389;
constexpr int DefaultAudioLatencyMs = 500;
constexpr int MaximumPort           = std::numeric_limits<std::uint16_t>::max();
constexpr int MaximumBitrateKbps    = std::numeric_limits<std::uint32_t>::max() / std::kilo::num;
class IntegerSetting {
public:
  using Field = std::uint32_t sdlrdp_config::*;
       IntegerSetting(Field field, std::string_view hint, int fallback, int minimum, int maximum)
      : _field{ field }, _hint{ hint }, _fallback{ fallback }, _minimum{ minimum }, _maximum{ maximum } { }
  auto Apply(Settings const& settings, sdlrdp_config& config) const -> void {
    config.*_field = Backend::Narrowed<std::uint32_t>(
        settings.Integer(std::string{ _hint }, _fallback, _minimum, _maximum));
  }
private:
  Field            _field;
  std::string_view _hint;
  int              _fallback;
  int              _minimum;
  int              _maximum;
};
auto Integers(Settings const& settings) -> sdlrdp_config {
  auto const    fields = std::to_array<IntegerSetting>(
      { { &sdlrdp_config::port            , SDL_HINT_RDP_PORT         , DefaultPort          , 0, MaximumPort    },
        { &sdlrdp_config::width           , SDL_HINT_RDP_WIDTH        , DefaultWidth         , 1, SDL_MAX_SINT32 },
        { &sdlrdp_config::height          , SDL_HINT_RDP_HEIGHT       , DefaultHeight        , 1, SDL_MAX_SINT32 },
        { &sdlrdp_config::audio_latency_ms, SDL_HINT_RDP_AUDIO_LATENCY, DefaultAudioLatencyMs, 0, SDL_MAX_SINT32 },
        { &sdlrdp_config::avc_bitrate_kbps, SDL_HINT_RDP_AVC_BITRATE, 0, 0, MaximumBitrateKbps } });
  sdlrdp_config config { };
  for (auto const& field : fields) field.Apply(settings, config);
  return config;
}
// The backend log callback carries an opaque context and a borrowed C string.
auto Log([[maybe_unused]] void* unused, sdlrdp_log_level level, char const* text) -> void {
  constexpr std::array priorities{ SDL_LOG_PRIORITY_ERROR, SDL_LOG_PRIORITY_WARN, SDL_LOG_PRIORITY_INFO };
  utilities::Expects(std::cmp_less(std::to_underlying(level), priorities.size()), "backend log level is known");
  utilities::Expects(text != nullptr, "backend log has text");
  SDL_LogMessage(SDL_LOG_CATEGORY_VIDEO, priorities.at(static_cast<std::size_t>(level)), "%s", text);
}
auto Authentication(std::optional<std::string> const& mode, bool has_password) -> sdlrdp_auth {
  switch (oxbox::utilities::HashString(mode.value_or(has_password ? "nla" : "none"))) {
  case "none"_hash: return SDLRDP_AUTH_NONE;
  case "tls"_hash:  return SDLRDP_AUTH_TLS;
  case "nla"_hash:  return SDLRDP_AUTH_NLA;
  default:          InvalidSetting("Invalid SDL_RDP_AUTH '" + mode.value_or("") + "'; valid names: none, tls, nla");
  }
}
}
Configuration::Configuration(Settings const& settings, decltype(sdlrdp_config::verify) verify,
                             decltype(sdlrdp_config::lookup) lookup, void* context)
    : _value{ Integers(settings) } {
  auto const names = std::to_array(
      { SDL_HINT_RDP_BIND, SDL_HINT_RDP_CERT_DIR, SDL_HINT_RDP_USER, SDL_HINT_RDP_PASSWORD, SDL_HINT_RDP_DOMAIN });
  auto const fields = std::to_array({ &sdlrdp_config::bind, &sdlrdp_config::cert_dir, &sdlrdp_config::user,
                                      &sdlrdp_config::password, &sdlrdp_config::domain });
  for (auto const& [name, field, text] : std::views::zip(names, fields, _strings)) {
    text = settings.Get(name);
    if (text) _value.*field = text->c_str();
  }
  _value.log = Log;
  _value.wait_for_client = settings.Boolean(SDL_HINT_RDP_WAIT_FOR_CLIENT, false);
  _value.codec = Codec(settings.Get(SDL_HINT_RDP_CODEC));
  _value.aspect = Aspect(settings.Get(SDL_HINT_RDP_ASPECT));
  _value.auth = Authentication(settings.Get(SDL_HINT_RDP_AUTH), _value.password != nullptr);
  _value.verify = verify;
  _value.lookup = lookup;
  _value.auth_user = context;
}
auto Configuration::Get() const -> sdlrdp_config const& {
  return _value;
}
}
