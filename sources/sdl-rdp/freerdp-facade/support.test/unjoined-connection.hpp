#pragma once
#include <sdl-rdp/freerdp-facade/channel-manager.hpp>
#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/utilities/posix.hpp>

#include <utility>

namespace sdl_rdp::freerdp_facade::support_test::detail::unjoined_connection {
using sdl_rdp::freerdp_facade::ChannelManager;
using sdl_rdp::freerdp_facade::Connection;
using sdl_rdp::utilities::ConnectedSockets;
using sdl_rdp::utilities::SocketPair;

// A server connection over a socket pair whose client never joins a channel, so every channel open is refused.
struct UnjoinedConnection {
  SocketPair     sockets   { ConnectedSockets()        };
  Connection     connection{ std::move(sockets.server) };
  ChannelManager channels  { connection.Context()      };
};
}

namespace sdl_rdp::freerdp_facade::support_test {
using detail::unjoined_connection::UnjoinedConnection;
}
