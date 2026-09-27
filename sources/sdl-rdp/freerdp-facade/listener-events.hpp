#pragma once
#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/freerdp-facade/failure-sink.hpp>

namespace sdl_rdp::freerdp_facade::detail::listener_events {
// What a listener's accept slot reports, installed by Listener's construction.
class ListenerEvents : public FailureSink {
public:
  virtual auto Accepted(Connection accepted) -> void = 0;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::listener_events::ListenerEvents;
}
