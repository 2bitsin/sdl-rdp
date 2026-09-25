#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <winpr/wtypes.h>
#include <cstdint>
#include <span>
#include <stop_token>
#include <thread>

namespace Backend {
class Authenticator;
class Departure;
class Diagnostics;
class FrameStore;
class PeerLink;
class PeerPump;
class PeerWait;
class SessionAccess;
class PeerLoop : private Pinned {
public:
  PeerLoop(PeerLink& link, SessionAccess& session, Diagnostics const& diagnostics, Authenticator const& authenticator,
           FrameStore& store, PeerWait& wait, PeerPump& pump, Departure& departure) noexcept;
  auto Start() -> void;
  auto Stop()  -> void;

private:
  auto Serve(std::stop_token const& quit)                                                          -> void;
  auto Run(std::stop_token const& quit)                                                            -> void;
  auto Configure()                                                                                 -> bool;
  auto Step(std::stop_token const& quit, std::span<WaitHandle> handles)                            -> bool;
  auto Dispatch(std::stop_token const& quit, std::span<WaitHandle> handles, std::uint32_t timeout) -> bool;
  PeerLink&            _link;
  SessionAccess&       _session;
  Diagnostics const&   _diagnostics;
  Authenticator const& _authenticator;
  FrameStore&          _store;
  PeerWait&            _wait;
  PeerPump&            _pump;
  Departure&           _departure;
  std::jthread         _thread;
};
}
