#pragma once
#include <sdl-rdp/freerdp-facade/listener-events.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/utilities/releases.hpp>
#include <sdl-rdp/utilities/socket.hpp>

#include <cstdint>
#include <memory>
#include <span>

struct rdp_freerdp_listener;

namespace sdl_rdp::freerdp_facade::detail::listener {
using sdl_rdp::utilities::Releases;
using sdl_rdp::utilities::Socket;
using sdl_rdp::utilities::SocketLibrary;

// abi: the release steps of a listener, handed the listener; it closes its sockets before it is freed.
auto ReleaseListener(rdp_freerdp_listener* listener) noexcept -> void;
using ListenerHandle = std::unique_ptr<rdp_freerdp_listener, Releases<ReleaseListener>>;
// FreeRDP's listener over a socket already bound and listening; each client it accepts reaches the events.
class Listener {
public:
       Listener(Socket socket, ListenerEvents& events);
  auto Port() const noexcept                            -> std::uint16_t;
  auto EventHandles(std::span<WaitHandle> budget) const -> std::span<WaitHandle>;
  auto Pump()                                           -> bool;

private:
  // FreeRDP owns the socket and never starts Winsock, so the listener holds the reference for it.
  SocketLibrary  _library;
  std::uint16_t  _port;
  ListenerHandle _listener;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::listener::Listener;
}
