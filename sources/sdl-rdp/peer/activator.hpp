#pragma once
#include <sdl-rdp/auth/forward.hpp>
#include <sdl-rdp/configuration/forward.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/peer/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/forward.hpp>

namespace sdl_rdp::peer::detail::activator {
using sdl_rdp::auth::Authenticator;
using sdl_rdp::configuration::Configuration;
using sdl_rdp::link::Activation;
using sdl_rdp::link::PeerLink;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::video::Encoder;

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

namespace sdl_rdp::peer {
using detail::activator::Activator;
}
