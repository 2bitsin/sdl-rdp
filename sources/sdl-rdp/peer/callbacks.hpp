#pragma once
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>

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
  auto InstallClient()                                  -> void;
  auto InstallAuthentication()                          -> void;
  auto InstallUpdates()                                 -> void;
  auto Failures(OperationName operation) const noexcept -> FailureLog;
  PeerLink&          _link;
  Authenticator&     _authenticator;
  Activator&         _activator;
  CapabilityCheck&   _capabilities;
  OutputControl&     _output;
  InputEvents const& _input;
};
}
