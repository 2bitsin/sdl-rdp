#pragma once
#include "pinned.hpp"
#include "rdp-handles.hpp"
#include "releases-listener.hpp"
#include "releases-peer.hpp"
#include "sdl-rdp-backend.h"

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
  unsigned Port() const noexcept;

private:
  void Accept(freerdp_peer* client);
  void Listen(std::stop_token const& quit);
  Diagnostics const& _diagnostics;
  Session&           _session;
  PeerFactory        _make;
  ListenerHandle     _listener;
  EventHandle        _stop;
  unsigned           _port       { };
  std::jthread       _thread;
};
}
