#pragma once
#include <sdl-rdp/utilities/pinned.hpp>

namespace Backend {
class Activation;
class Authenticator;
class DesktopLayout;
class Diagnostics;
class FramePacing;
class FrameStore;
class PeerLink;
class CapabilityCheck : private Pinned {
public:
       CapabilityCheck(PeerLink& link, Authenticator& authenticator, Activation const& activation, FramePacing& pacing,
                       DesktopLayout& desktop, FrameStore& store, Diagnostics const& diagnostics) noexcept;
  auto Accept() -> bool;

private:
  PeerLink&          _link;
  Authenticator&     _authenticator;
  Activation const&  _activation;
  FramePacing&       _pacing;
  DesktopLayout&     _desktop;
  FrameStore&        _store;
  Diagnostics const& _diagnostics;
};
}
