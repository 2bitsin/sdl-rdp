#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <freerdp/listener.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>
#include <thread>

namespace Backend {
class Configuration;
class Credentials;
class Diagnostics;
class Peer;
class Session;
// abi: release steps no single FreeRDP free function performs as a plain call.
auto CloseListener(freerdp_listener* listener) noexcept -> void;
using ListenerHandle = std::unique_ptr<freerdp_listener, Releases<CloseListener, freerdp_listener_free>>;
using PeerFactory    = std::move_only_function<std::unique_ptr<Peer>(PeerHandle)>;
class Listener : private Pinned {
public:
       Listener(Configuration const& configuration, Credentials const& credentials, Diagnostics const& diagnostics,
                Session& session, PeerFactory make);
  auto Port() const noexcept -> std::uint32_t;

private:
  auto Accept(PeerHandle accepted)         -> void;
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
