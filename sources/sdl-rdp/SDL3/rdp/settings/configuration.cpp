#include "configuration.hpp"
#include <sdl-rdp/utilities/wiped-string.hpp>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
namespace sdl3::rdp::settings::detail::configuration {
using sdl_rdp::configuration::AuthMode;
using sdl_rdp::settings::Settings;
using sdl_rdp::utilities::WipedString;
namespace {
auto ChosenAuth(Options const& options) -> AuthMode {
  return options.Get<&Settings::auth>().value_or(options.Get<&Settings::password>() ? AuthMode::Nla : AuthMode::None);
}
// SDL_RDP_TRACE is a diagnostic switch, not a setting: it has no hint and no file key.
auto Tracing() -> bool {
  auto const* trace = std::getenv("SDL_RDP_TRACE");
  return trace && std::string_view(trace) == "1";
}
auto AsPath(std::string const& text) -> std::filesystem::path {
  return text;
}
auto AsWiped(std::string const& text) -> WipedString {
  return WipedString{ text };
}
auto Built(Options const& options) -> Setup {
  return { .bind             = options.Get<&Settings::bind>(),
           .port             = options.Value<&Settings::port>().Get(),
           .cert_dir         = options.Get<&Settings::cert_dir>().transform(AsPath),
           .width            = options.Value<&Settings::width>().Get(),
           .height           = options.Value<&Settings::height>().Get(),
           .wait_for_client  = options.Value<&Settings::wait_for_client>(),
           .tracing          = Tracing(),
           .codec            = options.Value<&Settings::codec>(),
           .aspect           = options.Value<&Settings::aspect>().Ratio(),
           .audio_latency_ms = options.Value<&Settings::audio_latency>().Get(),
           .auth             = ChosenAuth(options),
           .user             = options.Get<&Settings::user>(),
           .password         = options.Get<&Settings::password>().transform(AsWiped),
           .domain           = options.Get<&Settings::domain>(),
           .avc_bitrate_kbps = options.Value<&Settings::avc_bitrate>().Get() };
}
}
Configuration::Configuration(Options const& options) : _setup{ Built(options) } { }
auto Configuration::Get() const noexcept -> Setup const& {
  return _setup;
}
auto Configuration::Width() const noexcept -> std::uint32_t {
  return _setup.width;
}
auto Configuration::Height() const noexcept -> std::uint32_t {
  return _setup.height;
}
auto Configuration::AudioLatency() const noexcept -> std::uint32_t {
  return _setup.audio_latency_ms;
}
}
