#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/configuration/authentication.hpp>
#include <sdl-rdp/configuration/refresh.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>

namespace Backend {
class Configuration {
public:
  explicit Configuration(sdlrdp_config const& config);
  auto     Config() const noexcept                             -> sdlrdp_config const&;
  auto     CertificateDirectory() const noexcept               -> std::filesystem::path const&;
  auto     Auth() const noexcept                               -> sdlrdp_auth;
  auto     Codec() const noexcept                              -> sdlrdp_codec;
  auto     SetCodec(sdlrdp_codec value) noexcept               -> void;
  auto     AvcBitrate() const noexcept                         -> std::uint32_t;
  auto     AudioLatency() const noexcept                       -> std::uint32_t;
  auto     RefreshPolicy() const noexcept                      -> Refresh const&;
  auto     SetRefresh(RefreshMode mode, std::uint32_t ceiling) -> void;

private:
  Authentication            _authentication;
  std::filesystem::path     _certificate_directory;
  std::atomic<sdlrdp_codec> _codec;
  std::uint32_t             _avc_bitrate_kbps;
  std::uint32_t             _audio_latency;
  Refresh                   _refresh;
};
}
