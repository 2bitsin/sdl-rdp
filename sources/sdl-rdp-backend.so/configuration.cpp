#include "_detail/configuration.hpp"

#include "_detail/certificate.hpp"
#include "_detail/contract.hpp"

#include <stdexcept>

namespace Backend {
namespace {
constexpr unsigned DefaultAudioLatency = 500;
std::filesystem::path CertificateDirectory(sdlrdp_config const& config) {
  return config.cert_dir ? std::filesystem::path(config.cert_dir) : DefaultCertificateDirectory();
}
}
Configuration::Configuration(sdlrdp_config const& config)
    : _authentication { config }, _credentials{ EnsureCertificate(CertificateDirectory(config)) },
      _codec{ config.codec }, _avc_bitrate_kbps{ config.avc_bitrate_kbps },
      _audio_latency{ config.audio_latency_ms ? config.audio_latency_ms : DefaultAudioLatency } { }
sdlrdp_config const& Configuration::Config() const noexcept {
  return _authentication.Config();
}
bool Configuration::InstallCredentials(rdpSettings& settings) const {
  try {
    InstallServerCredentials(settings, _credentials);
    return true;
  } catch (std::runtime_error const&) {
    return false;
  }
}
Credentials const& Configuration::ServerCredentials() const noexcept {
  return _credentials;
}
sdlrdp_auth Configuration::Auth() const noexcept {
  return Config().auth;
}
sdlrdp_codec Configuration::Codec() const noexcept {
  return _codec.load();
}
void Configuration::SetCodec(sdlrdp_codec value) noexcept {
  _codec.store(value);
}
unsigned Configuration::AvcBitrate() const noexcept {
  return _avc_bitrate_kbps;
}
unsigned Configuration::AudioLatency() const noexcept {
  return _audio_latency;
}
Refresh const& Configuration::RefreshPolicy() const noexcept {
  return _refresh;
}
void Configuration::SetRefresh(RefreshMode mode, unsigned ceiling) {
  Expects(ceiling > 0, "declared refresh is positive");
  _refresh = Refresh(mode, ceiling);
  _refresh.Restart();
}
}
