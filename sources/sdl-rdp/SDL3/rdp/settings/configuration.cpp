#include "configuration.hpp"
#include <sdl-rdp/utilities/contract.hpp>
#include <cstddef>
#include <utility>
namespace sdl3::rdp::settings::detail::configuration {
using sdl_rdp::settings::Settings;
namespace {
auto Numbers(Options const& options) -> sdlrdp_config {
  sdlrdp_config config{ };
  config.port             = options.Value<&Settings::port>().Get();
  config.width            = options.Value<&Settings::width>().Get();
  config.height           = options.Value<&Settings::height>().Get();
  config.audio_latency_ms = options.Value<&Settings::audio_latency>().Get();
  config.avc_bitrate_kbps = options.Value<&Settings::avc_bitrate>().Get();
  return config;
}
auto StringsFrom(Options const& options) -> ConfigurationStrings {
  return { { { &sdlrdp_config::bind    , options.Get<&Settings::bind>()     },
             { &sdlrdp_config::cert_dir, options.Get<&Settings::cert_dir>() },
             { &sdlrdp_config::user    , options.Get<&Settings::user>()     },
             { &sdlrdp_config::password, options.Get<&Settings::password>() },
             { &sdlrdp_config::domain  , options.Get<&Settings::domain>()   } } };
}
// The backend log callback carries an opaque context and a borrowed C string.
auto Log([[maybe_unused]] void* unused, sdlrdp_log_level level, char const* text) -> void {
  constexpr std::array priorities{ SDL_LOG_PRIORITY_ERROR, SDL_LOG_PRIORITY_WARN, SDL_LOG_PRIORITY_INFO };
  utilities::Expects(std::cmp_less(std::to_underlying(level), priorities.size()), "backend log level is known");
  utilities::Expects(text != nullptr, "backend log has text");
  SDL_LogMessage(SDL_LOG_CATEGORY_VIDEO, priorities.at(static_cast<std::size_t>(level)), "%s", text);
}
}
auto BackendAspect(sdl_rdp::settings::Aspect const& aspect) -> sdlrdp_aspect {
  return aspect.IsNone() ? sdlrdp_aspect{ } : aspect.Ratio();
}
Configuration::Configuration(Options const& options, decltype(sdlrdp_config::verify) verify,
                             decltype(sdlrdp_config::lookup) lookup, void* context)
    : _strings{ StringsFrom(options) }, _value{ Numbers(options) } {
  for (auto const& [field, text] : _strings)
    if (text) _value.*field = text->c_str();
  _value.log = Log;
  _value.wait_for_client = options.Value<&Settings::wait_for_client>();
  _value.codec = options.Value<&Settings::codec>();
  _value.aspect = BackendAspect(options.Value<&Settings::aspect>());
  _value.auth = options.Get<&Settings::auth>().value_or(_value.password ? SDLRDP_AUTH_NLA : SDLRDP_AUTH_NONE);
  _value.verify = verify;
  _value.lookup = lookup;
  _value.auth_user = context;
}
auto Configuration::Get() const -> sdlrdp_config const& {
  return _value;
}
}
