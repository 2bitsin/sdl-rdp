#pragma once
#include <sdl-rdp/auth/forward.hpp>
#include <sdl-rdp/configuration/forward.hpp>
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/peer/forward.hpp>
#include <sdl-rdp/session/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <freerdp/listener.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>
#include <thread>

namespace sdl_rdp::session::detail::listener {
using sdl_rdp::auth::Credentials;
using sdl_rdp::configuration::Configuration;
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::freerdp_facade::EventHandle;
using sdl_rdp::freerdp_facade::PeerHandle;
using sdl_rdp::peer::Peer;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Releases;

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

namespace sdl_rdp::session {
using detail::listener::Listener;
}
