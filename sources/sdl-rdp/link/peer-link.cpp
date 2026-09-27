#include <sdl-rdp/link/peer-link.hpp>

#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>

#include <utility>

namespace sdl_rdp::link::detail::peer_link {
using sdl_rdp::freerdp_facade::ManualResetEvent;
using sdl_rdp::freerdp_facade::StringKey;

PeerLink::PeerLink(freerdp_facade::Connection accepted)
    : _connection{ std::move(accepted) }, _wake{ ManualResetEvent("Peer wake event") },
      _channels{ _connection.Context() } { }
auto PeerLink::Connection() const noexcept -> freerdp_facade::Connection const& {
  return _connection;
}
auto PeerLink::Connection() noexcept -> freerdp_facade::Connection& {
  return _connection;
}
auto PeerLink::Channels() const noexcept -> ChannelManager const& {
  return _channels;
}
auto PeerLink::Channels() noexcept -> ChannelManager& {
  return _channels;
}
auto PeerLink::Dynamic() noexcept -> DynamicChannels& {
  return _dynamic;
}
auto PeerLink::Signal() -> void {
  _wake.Transition(WakeEvent::Phase::Pending);
}
auto PeerLink::Settle() -> void {
  _wake.Transition(WakeEvent::Phase::Idle);
}
auto PeerLink::Wake() const -> WaitHandle {
  return _wake.Handle();
}
auto PeerLink::Invalidate() noexcept -> void {
  _handle_count = 0;
}
// The name the client announced, else the address the listener accepted it from.
auto ClientHostname(PeerLink const& link) -> std::string {
  auto const& connection = link.Connection();
  return std::string{ connection.Settings().Get(StringKey::ClientHostname).value_or(connection.Hostname()) };
}
}
