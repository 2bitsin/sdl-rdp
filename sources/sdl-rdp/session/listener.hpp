#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/releases-listener.hpp>
#include <sdl-rdp/freerdp-facade/releases-peer.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>
#include <thread>

namespace Backend {
class Configuration;
class Diagnostics;
class Peer;
class Session;
using PeerFactory = std::move_only_function<std::unique_ptr<Peer>(PeerHandle)>;
class Listener : private Pinned {
public:
       Listener(Configuration const& configuration, Diagnostics const& diagnostics, Session& session, PeerFactory make);
  auto Port() const noexcept -> std::uint32_t;

private:
  auto Accept(freerdp_peer* client)        -> void;
  auto Listen(std::stop_token const& quit) -> void;
  Diagnostics const& _diagnostics;
  Session&           _session;
  PeerFactory        _make;
  ListenerHandle     _listener;
  EventHandle        _stop;
  std::uint32_t      _port       { };
  std::jthread       _thread;
};
}
