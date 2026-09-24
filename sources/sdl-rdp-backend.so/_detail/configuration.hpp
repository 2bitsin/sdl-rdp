#pragma once
#include "authentication.hpp"
#include "credentials.hpp"
#include "refresh.hpp"
#include "sdl-rdp-backend.h"

#include <atomic>
#include <freerdp/settings.h>

namespace Backend {
class Configuration {
public:
  explicit             Configuration(sdlrdp_config const& config);
  sdlrdp_config const& Config() const               noexcept;
  bool                 InstallCredentials(rdpSettings& settings) const;
  Credentials const&   ServerCredentials() const    noexcept;
  sdlrdp_auth          Auth() const                 noexcept;
  sdlrdp_codec         Codec() const                noexcept;
  void                 SetCodec(sdlrdp_codec value) noexcept;
  unsigned             AvcBitrate() const           noexcept;
  unsigned             AudioLatency() const         noexcept;
  Refresh const&       RefreshPolicy() const        noexcept;
  void                 SetRefresh(RefreshMode mode, unsigned ceiling);

private:
  Authentication            _authentication;
  Credentials               _credentials;
  std::atomic<sdlrdp_codec> _codec;
  unsigned                  _avc_bitrate_kbps;
  unsigned                  _audio_latency;
  Refresh                   _refresh;
};
}
