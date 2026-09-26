#pragma once
#include <sdl-rdp/auth/forward.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/input/forward.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/peer/forward.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/forward.hpp>

#include <cstdint>

namespace sdl_rdp::peer::detail::callbacks {
using sdl_rdp::auth::Authenticator;
using sdl_rdp::diagnostics::FailuresThrough;
using sdl_rdp::input::InputEvents;
using sdl_rdp::link::PeerLink;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::video::OutputControl;

class PeerCallbacks : private Pinned {
public:
  PeerCallbacks(PeerLink& link, Authenticator& authenticator, Activator& activator, CapabilityCheck& capabilities,
                OutputControl& output, InputEvents& input);
  ~PeerCallbacks();

private:
  auto InstallClient()                -> void;
  auto InstallAuthentication()        -> void;
  auto InstallUpdates()               -> void;
  auto Activate()                     -> bool;
  auto Capabilities()                 -> bool;
  auto Acknowledge(std::uint32_t id)  -> bool;
  auto Suppress(std::uint8_t allow)   -> bool;
  auto FailureSource() const noexcept -> InputEvents const&;
  PeerLink&             _link;
  Authenticator&        _authenticator;
  Activator&            _activator;
  CapabilityCheck&      _capabilities;
  OutputControl&        _output;
  InputEvents const&    _input;
  static constexpr auto _failures      = FailuresThrough<&PeerCallbacks::FailureSource>;
};
}

namespace sdl_rdp::peer {
using detail::callbacks::PeerCallbacks;
}
