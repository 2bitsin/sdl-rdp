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
                 PeerCallbacks(PeerCallbacks const&) = delete;
                 PeerCallbacks(PeerCallbacks&&)      = delete;
  PeerCallbacks(PeerLink& link, Authenticator& authenticator, Activator& activator, CapabilityCheck& capabilities,
                OutputControl& output, InputEvents& input);
                 ~PeerCallbacks();
  PeerCallbacks& operator = (PeerCallbacks const&)   = delete;
  PeerCallbacks& operator = (PeerCallbacks&&)        = delete;

private:
  void InstallClient();
  void InstallUpdates();
  PeerLink&        _link;
  Authenticator&   _authenticator;
  Activator&       _activator;
  CapabilityCheck& _capabilities;
  OutputControl&   _output;
};
}
