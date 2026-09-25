#include <sdl-rdp/link/peer-link.hpp>

#include <sdl-rdp/freerdp-facade/manual-reset-event.hpp>
#include <sdl-rdp/link/exceptions.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/terminated-copy.hpp>

#include <freerdp/channels/wtsvc.h>
#include <freerdp/svc.h>
#include <winpr/synch.h>
#include <array>
#include <cstdint>
#include <utility>

namespace Backend {
namespace {
auto Accepted(PeerHandle accepted) -> PeerHandle {
  Expects(accepted != nullptr, "accepted peer exists");
  return accepted;
}
auto OpenChannelManager(rdpContext& context) -> ChannelManager {
  // FreeRDP 3.32 server.c:1134 WTSOpenServerA takes the peer's rdpContext through its server-name parameter.
  auto* opened = WTSOpenServerA(reinterpret_cast<char*>(&context));
  if (!opened || opened == INVALID_HANDLE_VALUE) throw AllocationFailed{ "Channel manager" };
  return ChannelManager{ opened };
}
}
PeerLink::PeerLink(PeerHandle accepted)
    : _client{ Accepted(std::move(accepted)) }, _socket{ _client->sockfd },
      _wake{ sdl_rdp::freerdp_facade::ManualResetEvent("Peer wake event") } {
  if (!freerdp_peer_context_new(_client.get())) throw sdl_rdp::link::PeerContextFailed{ "Session" };
  Expects(_client->context != nullptr, "the peer context exists once created");
  Expects(_client->context->update != nullptr, "the peer context carries its update table");
  _channels = OpenChannelManager(*_client->context);
}
auto PeerLink::Client() const noexcept -> freerdp_peer& {
  return *_client;
}
auto PeerLink::Context() const noexcept -> rdpContext& {
  return *_client->context;
}
auto PeerLink::Settings() const noexcept -> rdpSettings& {
  return *_client->context->settings;
}
auto PeerLink::Channels() const noexcept -> WaitHandle {
  return _channels.get();
}
auto PeerLink::Dynamic() noexcept -> DynamicChannels& {
  return _dynamic;
}
auto PeerLink::Socket() const noexcept -> int {
  return _socket;
}
auto PeerLink::WriteBlocked() const -> bool {
  return _client->IsWriteBlocked(_client.get());
}
auto PeerLink::Signal() -> void {
  _wake.Transition(WakeEvent::Phase::Pending);
}
auto PeerLink::Settle() -> void {
  _wake.Transition(WakeEvent::Phase::Idle);
}
auto PeerLink::Wake() const noexcept -> WaitHandle {
  return _wake.get();
}
auto PeerLink::Invalidate() noexcept -> void {
  _handle_count = 0;
}
auto PeerLink::Refuse(std::uint32_t reason) -> void {
  freerdp_set_error_info(_client->context->rdp, reason);
  freerdp_send_error_info(_client->context->rdp);
}
auto PeerLink::Close() -> void {
  _client->Close(_client.get());
}
auto DynamicChannelsReady(PeerLink const& link) -> bool {
  return WTSVirtualChannelManagerGetDrdynvcState(link.Channels()) == DRDYNVC_STATE_READY;
}
auto Joined(PeerLink const& link, std::string_view name) -> bool {
  Expects(name.size() <= CHANNEL_NAME_LEN, "a static channel name fits its protocol field");
  std::array<char, CHANNEL_NAME_LEN + 1> terminated{ };
  CopyTerminated(terminated, name);
  return WTSVirtualChannelManagerIsChannelJoined(link.Channels(), terminated.data());
}
}
