#pragma once
#include <sdl-rdp/auth/forward.hpp>
#include <sdl-rdp/configuration/forward.hpp>
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/diagnostics/logged-failures.hpp>
#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/freerdp-facade/listener-events.hpp>
#include <sdl-rdp/freerdp-facade/listener.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/peer/forward.hpp>
#include <sdl-rdp/session/forward.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <stop_token>
#include <thread>

namespace sdl_rdp::session::detail::listener {
using sdl_rdp::auth::Credentials;
using sdl_rdp::configuration::Configuration;
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::diagnostics::LoggedFailures;
using sdl_rdp::freerdp_facade::Connection;
using sdl_rdp::freerdp_facade::EventHandle;
using sdl_rdp::freerdp_facade::ListenerEvents;
using sdl_rdp::peer::Peer;

using PeerFactory = std::move_only_function<std::unique_ptr<Peer>(Connection)>;
class Listener final : public LoggedFailures<ListenerEvents> {
public:
       Listener(Configuration const& configuration, Credentials const& credentials, Diagnostics const& diagnostics,
                Session& session, PeerFactory make);
  auto Port() const noexcept -> std::uint32_t;

private:
  auto Accepted(Connection accepted)       -> void override;
  auto Listen(std::stop_token const& quit) -> void;
  Session&                 _session;
  PeerFactory              _make;
  freerdp_facade::Listener _listener;
  EventHandle              _stop;
  std::jthread             _thread;
};
}

namespace sdl_rdp::session {
using detail::listener::Listener;
}
