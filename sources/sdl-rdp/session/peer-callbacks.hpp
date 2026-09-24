#pragma once

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
  auto InstallClient()  -> void;
  auto InstallUpdates() -> void;
  PeerLink&        _link;
  Authenticator&   _authenticator;
  Activator&       _activator;
  CapabilityCheck& _capabilities;
  OutputControl&   _output;
};
}
