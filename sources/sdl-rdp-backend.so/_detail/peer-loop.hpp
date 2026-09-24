#pragma once
#include "pinned.hpp"

#include <span>
#include <stop_token>
#include <thread>
#include <winpr/wtypes.h>

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
  void Start();
  void Stop();

private:
  void Serve(std::stop_token const& quit);
  bool Run(std::stop_token const& quit);
  bool Configure();
  bool Step(std::stop_token const& quit, std::span<HANDLE> handles);
  bool Dispatch(std::stop_token const& quit, std::span<HANDLE> handles, DWORD timeout);
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
