#include <sdl-rdp/link/peer-link.hpp>

#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/text.hpp>

#include <freerdp/channels/wtsvc.h>
#include <freerdp/svc.h>
#include <array>
#include <cstdint>
#include <utility>

namespace sdl_rdp::link::detail::peer_link {
using sdl_rdp::freerdp_facade::ManualResetEvent;
using sdl_rdp::freerdp_facade::StringKey;
using sdl_rdp::utilities::AllocationFailed;
using sdl_rdp::utilities::CopyTerminated;
using sdl_rdp::utilities::Expects;

namespace {
auto OpenChannelManager(rdpContext& context) -> ChannelManager {
  // FreeRDP 3.32 server.c:1134 WTSOpenServerA takes the peer's rdpContext through its server-name parameter.
  auto* opened = WTSOpenServerA(reinterpret_cast<char*>(&context));
  if (!opened || opened == INVALID_HANDLE_VALUE) throw AllocationFailed{ "Channel manager" };
  return ChannelManager{ opened };
}
}
PeerLink::PeerLink(freerdp_facade::Connection accepted)
    : _connection{ std::move(accepted) }, _wake{ ManualResetEvent("Peer wake event") },
      _channels{ OpenChannelManager(_connection.Context()) } { }
auto PeerLink::Connection() const noexcept -> freerdp_facade::Connection const& {
  return _connection;
}
auto PeerLink::Connection() noexcept -> freerdp_facade::Connection& {
  return _connection;
}
auto PeerLink::Channels() const noexcept -> ChannelManager const& {
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
auto DynamicChannelsReady(PeerLink const& link) -> bool {
  return WTSVirtualChannelManagerGetDrdynvcState(link.Channels().get()) == DRDYNVC_STATE_READY;
}
auto Joined(PeerLink const& link, std::string_view name) -> bool {
  Expects(name.size() <= CHANNEL_NAME_LEN, "a static channel name fits its protocol field");
  std::array<char, CHANNEL_NAME_LEN + 1> terminated{ };
  CopyTerminated(terminated, name);
  return WTSVirtualChannelManagerIsChannelJoined(link.Channels().get(), terminated.data());
}
}
