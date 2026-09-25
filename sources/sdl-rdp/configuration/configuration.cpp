#include <sdl-rdp/configuration/configuration.hpp>

#include <sdl-rdp/configuration/exceptions.hpp>
#include <sdl-rdp/configuration/validation.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>

namespace sdl_rdp::configuration::detail::configuration {
namespace {
constexpr std::uint32_t DefaultAudioLatency = 500;
auto DefaultCertificateDirectory() -> std::filesystem::path {
  if (auto* data = std::getenv("XDG_DATA_HOME"); data && *data) return std::filesystem::path(data) / "sdl-rdp";
  if (auto* home = std::getenv("HOME"); home && *home) return std::filesystem::path(home) / ".local/share/sdl-rdp";
  std::array<char, 16384> buffer { };
  passwd                  entry  { };
  passwd*                 found  = nullptr;
  if (getpwuid_r(getuid(), &entry, buffer.data(), buffer.size(), &found) || !found) throw HomeUnavailable{ };
  return std::filesystem::path(entry.pw_dir) / ".local/share/sdl-rdp";
}
auto Validated(Setup const& setup) -> Setup {
  Validate(setup);
  return setup;
}
}
Configuration::Configuration(Setup const& setup, CredentialCheck const& credentials)
    : _setup{ Validated(setup) }, _credentials{ credentials },
      _certificate_directory{ setup.cert_dir.value_or(DefaultCertificateDirectory()) }, _codec{ setup.codec },
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
