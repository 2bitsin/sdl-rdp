#pragma once
#include <sdl-rdp/freerdp-facade/connection.hpp>

namespace sdl_rdp::freerdp_facade::detail::connection {
// A test's way to a connection's raw context, inline because the unit and integration suites share no source.
class ConnectionProbe {
public:
  static auto Context(Connection& connection) noexcept -> rdp_context& {
    return connection.Context();
  }
};
}

namespace sdl_rdp::freerdp_facade {
using detail::connection::ConnectionProbe;
}
