#pragma once
#include <sdl-rdp/utilities/pinned.hpp>

namespace Backend {
class Activation;
class Arrival;
class Authenticator;
class Configuration;
class Encoder;
class PeerLink;
class Activator : private Pinned {
public:
       Activator(PeerLink& link, Authenticator& authenticator, Activation const& activation, Encoder& encoder,
                 Configuration const& configuration, Arrival& arrival) noexcept;
  auto Activate() -> bool;

private:
  PeerLink&            _link;
  Authenticator&       _authenticator;
  Activation const&    _activation;
  Encoder&             _encoder;
  Configuration const& _configuration;
  Arrival&             _arrival;
};
}
