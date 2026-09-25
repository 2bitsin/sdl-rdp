#include "configuration.hpp"
#include <sdl-rdp/utilities/contract.hpp>
#include <cstddef>
#include <utility>
namespace sdl3::rdp::settings::detail::configuration {
using sdl_rdp::settings::Settings;
using sdl_rdp::utilities::Expects;
namespace {
// The backend log callback carries an opaque context and a borrowed C string.
auto Log([[maybe_unused]] void* unused, sdlrdp_log_level level, char const* text) -> void {
  constexpr std::array priorities{ SDL_LOG_PRIORITY_ERROR, SDL_LOG_PRIORITY_WARN, SDL_LOG_PRIORITY_INFO };
  Expects(std::cmp_less(std::to_underlying(level), priorities.size()), "backend log level is known");
  Expects(text != nullptr, "backend log has text");
  SDL_LogMessage(SDL_LOG_CATEGORY_VIDEO, priorities.at(static_cast<std::size_t>(level)), "%s", text);
}
}
auto BackendAspect(Aspect const& aspect) -> sdlrdp_aspect {
  return aspect.IsNone() ? sdlrdp_aspect{ } : aspect.Ratio();
}
Configuration::Configuration(Options const& options)
    : _text{ options }, _port{ options.Value<&Settings::port>().Get() },
      _width{ options.Value<&Settings::width>().Get() }, _height{ options.Value<&Settings::height>().Get() },
      _audio_latency_ms{ options.Value<&Settings::audio_latency>().Get() },
      _avc_bitrate_kbps{ options.Value<&Settings::avc_bitrate>().Get()   },
      _wait_for_client{ options.Value<&Settings::wait_for_client>() }, _codec{ options.Value<&Settings::codec>() },
      _aspect{ BackendAspect(options.Value<&Settings::aspect>()) },
      _auth{ options.Get<&Settings::auth>().value_or(options.Get<&Settings::password>() ? SDLRDP_AUTH_NLA
                                                                                        : SDLRDP_AUTH_NONE) } { }
auto Configuration::Get() const -> sdlrdp_config {
  sdlrdp_config config{ };
  _text.Fill(config);
  config.port             = _port;
  config.width            = _width;
  config.height           = _height;
  config.audio_latency_ms = _audio_latency_ms;
  config.avc_bitrate_kbps = _avc_bitrate_kbps;
  config.wait_for_client  = int{ _wait_for_client };
  config.codec            = _codec;
  config.aspect           = _aspect;
  config.auth             = _auth;
  config.log              = Log;
  return config;
}
auto Configuration::Width() const -> std::uint32_t {
  return _width;
}
auto Configuration::Height() const -> std::uint32_t {
  return _height;
}
auto Configuration::AudioLatency() const -> std::uint32_t {
  return _audio_latency_ms;
}
}
