#include <sdl-rdp/configuration/configuration.hpp>

#include <sdl-rdp/configuration/user-data.hpp>
#include <sdl-rdp/configuration/validation.hpp>

#include <cstdint>

namespace sdl_rdp::configuration::detail::configuration {
namespace {
constexpr std::uint32_t DefaultAudioLatency = 500;
auto Validated(Setup const& setup) -> Setup {
  Validate(setup);
  return setup;
}
}
Configuration::Configuration(Setup const& setup, CredentialCheck const& credentials)
    : _setup{ Validated(setup) }, _credentials{ credentials },
      _certificate_directory{ setup.cert_dir.value_or(UserDataDirectory()) }, _codec{ setup.codec },
      _audio_latency{ setup.audio_latency_ms ? setup.audio_latency_ms : DefaultAudioLatency } { }
auto Configuration::Credentials() const noexcept -> CredentialCheck const& {
  return _credentials;
}
auto Configuration::Config() const noexcept -> Setup const& {
  return _setup;
}
auto Configuration::CertificateDirectory() const noexcept -> std::filesystem::path const& {
  return _certificate_directory;
}
auto Configuration::Auth() const noexcept -> AuthMode {
  return _setup.auth;
}
auto Configuration::CodecPreference() const noexcept -> Codec {
  return _codec.load();
}
auto Configuration::SetCodec(Codec value) -> void {
  ValidateCodec(value);
  _codec.store(value);
}
auto Configuration::AvcBitrate() const noexcept -> std::uint32_t {
  return _setup.avc_bitrate_kbps;
}
auto Configuration::AudioLatency() const noexcept -> std::uint32_t {
  return _audio_latency;
}
auto Configuration::RefreshPolicy() const noexcept -> Refresh const& {
  return _refresh;
}
auto Configuration::SetRefresh(RefreshMode mode, std::uint32_t ceiling) -> void {
  ValidateRefresh(mode, ceiling);
  _refresh = Refresh(mode, ceiling);
  _refresh.Restart();
}
}
