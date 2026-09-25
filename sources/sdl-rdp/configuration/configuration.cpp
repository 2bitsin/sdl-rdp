#include <sdl-rdp/configuration/configuration.hpp>

#include <sdl-rdp/configuration/exceptions.hpp>
#include <sdl-rdp/configuration/validation.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <pwd.h>
#include <unistd.h>

namespace Backend {
namespace {
constexpr std::uint32_t DefaultAudioLatency = 500;
auto DefaultCertificateDirectory() -> std::filesystem::path {
  if (auto* data = std::getenv("XDG_DATA_HOME"); data && *data) return std::filesystem::path(data) / "sdl-rdp";
  if (auto* home = std::getenv("HOME"); home && *home) return std::filesystem::path(home) / ".local/share/sdl-rdp";
  std::array<char, 16384> buffer { };
  passwd                  entry  { };
  passwd*                 found  = nullptr;
  if (getpwuid_r(getuid(), &entry, buffer.data(), buffer.size(), &found) || !found)
    throw sdl_rdp::configuration::HomeUnavailable{ };
  return std::filesystem::path(entry.pw_dir) / ".local/share/sdl-rdp";
}
auto ChosenDirectory(sdlrdp_config const& config) -> std::filesystem::path {
  return config.cert_dir ? std::filesystem::path(config.cert_dir) : DefaultCertificateDirectory();
}
}
Configuration::Configuration(sdlrdp_config const& config)
    : _authentication{ config }, _certificate_directory{ ChosenDirectory(config) }, _codec{ config.codec },
      _avc_bitrate_kbps{ config.avc_bitrate_kbps                                                 },
      _audio_latency   { config.audio_latency_ms ? config.audio_latency_ms : DefaultAudioLatency } { }
auto Configuration::Config() const noexcept -> sdlrdp_config const& {
  return _authentication.Config();
}
auto Configuration::CertificateDirectory() const noexcept -> std::filesystem::path const& {
  return _certificate_directory;
}
auto Configuration::Auth() const noexcept -> sdlrdp_auth {
  return Config().auth;
}
auto Configuration::Codec() const noexcept -> sdlrdp_codec {
  return _codec.load();
}
auto Configuration::SetCodec(sdlrdp_codec value) -> void {
  sdl_rdp::configuration::ValidateCodec(value);
  _codec.store(value);
}
auto Configuration::AvcBitrate() const noexcept -> std::uint32_t {
  return _avc_bitrate_kbps;
}
auto Configuration::AudioLatency() const noexcept -> std::uint32_t {
  return _audio_latency;
}
auto Configuration::RefreshPolicy() const noexcept -> Refresh const& {
  return _refresh;
}
auto Configuration::SetRefresh(std::uint32_t mode, std::uint32_t ceiling) -> void {
  _refresh = Refresh(sdl_rdp::configuration::ValidRefresh(mode, ceiling), ceiling);
  _refresh.Restart();
}
}
