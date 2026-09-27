#pragma once
#include <sdl-rdp/freerdp-facade/listener-events.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/utilities/posix.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <cstdint>
#include <memory>
#include <span>

struct rdp_freerdp_listener;

namespace sdl_rdp::freerdp_facade::detail::listener {
using sdl_rdp::utilities::Descriptor;
using sdl_rdp::utilities::Releases;

// abi: the release steps of a listener, handed the listener; it closes its sockets before it is freed.
auto ReleaseListener(rdp_freerdp_listener* listener) noexcept -> void;
using ListenerHandle = std::unique_ptr<rdp_freerdp_listener, Releases<ReleaseListener>>;
// FreeRDP's listener over a socket already bound and listening; each client it accepts reaches the events.
class Listener {
public:
       Listener(Descriptor socket, ListenerEvents& events);
  auto Port() const noexcept                            -> std::uint16_t;
  auto EventHandles(std::span<WaitHandle> budget) const -> std::span<WaitHandle>;
  auto Pump()                                           -> bool;

private:
  std::uint16_t  _port;
  ListenerHandle _listener;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::listener::Listener;
}
