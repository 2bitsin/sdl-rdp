#pragma once
#include <sdl-rdp/utilities/pinned.hpp>

#include <winpr/wtypes.h>
#include <span>
#include <stop_token>
#include <thread>

namespace Backend {
class Configuration;
class Departure;
class Diagnostics;
class FrameStore;
class PeerLink;
class PeerPump;
class PeerWait;
class SessionAccess;
class PeerLoop : private Pinned {
public:
  PeerLoop(PeerLink& link, SessionAccess& session, Diagnostics const& diagnostics, Configuration const& configuration,
           FrameStore& store, PeerWait& wait, PeerPump& pump, Departure& departure) noexcept;
  auto Start() -> void;
  auto Stop()  -> void;

private:
  auto Serve(std::stop_token const& quit)                                              -> void;
  auto Run(std::stop_token const& quit)                                                -> bool;
  auto Configure()                                                                     -> bool;
  auto Step(std::stop_token const& quit, std::span<HANDLE> handles)                    -> bool;
  auto Dispatch(std::stop_token const& quit, std::span<HANDLE> handles, DWORD timeout) -> bool;
  PeerLink&            _link;
  SessionAccess&       _session;
  Diagnostics const&   _diagnostics;
  Configuration const& _configuration;
  FrameStore&          _store;
  PeerWait&            _wait;
  PeerPump&            _pump;
  Departure&           _departure;
  std::jthread         _thread;
};
}
