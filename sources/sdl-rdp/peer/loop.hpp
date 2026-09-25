#pragma once
#include <sdl-rdp/auth/forward.hpp>
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/peer/forward.hpp>
#include <sdl-rdp/picture/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <winpr/wtypes.h>
#include <cstdint>
#include <span>
#include <stop_token>
#include <thread>

namespace sdl_rdp::peer::detail::loop {
using sdl_rdp::auth::Authenticator;
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::link::PeerLink;
using sdl_rdp::link::SessionAccess;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::utilities::Pinned;

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

namespace sdl_rdp::peer {
using detail::loop::PeerLoop;
}
