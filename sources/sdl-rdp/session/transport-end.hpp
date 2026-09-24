#pragma once
#include <sdl-rdp/utilities/pinned.hpp>

namespace Backend {
class Activation;
class Authenticator;
class Diagnostics;
class FrameStore;
class PeerFrames;
class PeerLink;
class TransportEnd : private Pinned {
public:
  TransportEnd(PeerLink& link, Activation const& activation, Authenticator& authenticator, PeerFrames const& frames,
               FrameStore& store, Diagnostics const& diagnostics) noexcept;
  auto Report() -> void;

private:
  auto SecurityEnded() const -> bool;
  auto PendingOutput() const -> bool;
  PeerLink&          _link;
  Activation const&  _activation;
  Authenticator&     _authenticator;
  PeerFrames const&  _frames;
  FrameStore&        _store;
  Diagnostics const& _diagnostics;
};
}
