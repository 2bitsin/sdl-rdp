#include "_detail/configuration.hpp"

#include "_detail/certificate.hpp"
#include "_detail/contract.hpp"

#include <stdexcept>

namespace Backend {
namespace {
constexpr unsigned DefaultAudioLatency = 500;
auto CertificateDirectory(sdlrdp_config const& config) -> std::filesystem::path {
  return config.cert_dir ? std::filesystem::path(config.cert_dir) : DefaultCertificateDirectory();
}
}
Configuration::Configuration(sdlrdp_config const& config)
    : _authentication { config }, _credentials{ EnsureCertificate(CertificateDirectory(config)) },
      _codec{ config.codec }, _avc_bitrate_kbps{ config.avc_bitrate_kbps },
      _audio_latency{ config.audio_latency_ms ? config.audio_latency_ms : DefaultAudioLatency } { }
auto Configuration::Config() const noexcept -> sdlrdp_config const& {
  return _authentication.Config();
}
auto Configuration::InstallCredentials(rdpSettings& settings) const -> bool {
  try {
    InstallServerCredentials(settings, _credentials);
    return true;
  } catch (std::runtime_error const&) {
    return false;
  }
}
auto Configuration::ServerCredentials() const noexcept -> Credentials const& {
  return _credentials;
}
auto Configuration::Auth() const noexcept -> sdlrdp_auth {
  return Config().auth;
}
auto Configuration::Codec() const noexcept -> sdlrdp_codec {
  return _codec.load();
}
auto Configuration::SetCodec(sdlrdp_codec value) noexcept -> void {
  _codec.store(value);
}
auto Configuration::AvcBitrate() const noexcept -> unsigned {
  return _avc_bitrate_kbps;
}
auto Configuration::AudioLatency() const noexcept -> unsigned {
  return _audio_latency;
}
auto Configuration::RefreshPolicy() const noexcept -> Refresh const& {
  return _refresh;
}
auto Configuration::SetRefresh(RefreshMode mode, unsigned ceiling) -> void {
  Expects(ceiling > 0, "declared refresh is positive");
  _refresh = Refresh(mode, ceiling);
  _refresh.Restart();
}
}
