#pragma once
#include "authentication.hpp"
#include "credentials.hpp"
#include "refresh.hpp"
#include "sdl-rdp-backend.h"

#include <freerdp/settings.h>
#include <atomic>

namespace Backend {
class Configuration {
public:
  explicit Configuration(sdlrdp_config const& config);
  auto     Config() const noexcept                         -> sdlrdp_config const&;
  auto     InstallCredentials(rdpSettings& settings) const -> bool;
  auto     ServerCredentials() const noexcept              -> Credentials const&;
  auto     Auth() const noexcept                           -> sdlrdp_auth;
  auto     Codec() const noexcept                          -> sdlrdp_codec;
  auto     SetCodec(sdlrdp_codec value) noexcept           -> void;
  auto     AvcBitrate() const noexcept                     -> unsigned;
  auto     AudioLatency() const noexcept                   -> unsigned;
  auto     RefreshPolicy() const noexcept                  -> Refresh const&;
  auto     SetRefresh(RefreshMode mode, unsigned ceiling)  -> void;

private:
  Authentication            _authentication;
  Credentials               _credentials;
  std::atomic<sdlrdp_codec> _codec;
  unsigned                  _avc_bitrate_kbps;
  unsigned                  _audio_latency;
  Refresh                   _refresh;
};
}
