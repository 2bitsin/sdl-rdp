#pragma once
#include <sdl-rdp/core/authentication.hpp>
#include <sdl-rdp/core/credentials.hpp>
#include <sdl-rdp/core/refresh.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <freerdp/settings.h>
#include <atomic>
#include <cstdint>

namespace Backend {
class Configuration {
public:
  explicit Configuration(sdlrdp_config const& config);
  auto     Config() const noexcept                             -> sdlrdp_config const&;
  auto     InstallCredentials(rdpSettings& settings) const     -> bool;
  auto     ServerCredentials() const noexcept                  -> Credentials const&;
  auto     Auth() const noexcept                               -> sdlrdp_auth;
  auto     Codec() const noexcept                              -> sdlrdp_codec;
  auto     SetCodec(sdlrdp_codec value) noexcept               -> void;
  auto     AvcBitrate() const noexcept                         -> std::uint32_t;
  auto     AudioLatency() const noexcept                       -> std::uint32_t;
  auto     RefreshPolicy() const noexcept                      -> Refresh const&;
  auto     SetRefresh(RefreshMode mode, std::uint32_t ceiling) -> void;

private:
  Authentication            _authentication;
  Credentials               _credentials;
  std::atomic<sdlrdp_codec> _codec;
  std::uint32_t             _avc_bitrate_kbps;
  std::uint32_t             _audio_latency;
  Refresh                   _refresh;
};
}
