#pragma once
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>

#include <cstdint>

namespace Backend {
class Activator;
class Authenticator;
class CapabilityCheck;
class InputEvents;
class OutputControl;
class PeerLink;
class PeerCallbacks {
public:
       PeerCallbacks(PeerCallbacks const&)               = delete;
       PeerCallbacks(PeerCallbacks&&)                    = delete;
       PeerCallbacks(PeerLink& link, Authenticator& authenticator, Activator& activator, CapabilityCheck& capabilities,
                     OutputControl& output, InputEvents& input);
       ~PeerCallbacks();
  auto operator=(PeerCallbacks const&) -> PeerCallbacks& = delete;
  auto operator=(PeerCallbacks&&)      -> PeerCallbacks& = delete;

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
