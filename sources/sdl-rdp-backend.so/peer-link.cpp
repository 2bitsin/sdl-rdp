#include "_detail/peer-link.hpp"

#include "_detail/contract.hpp"

#include <freerdp/channels/wtsvc.h>
#include <winpr/synch.h>
#include <stdexcept>
#include <utility>

namespace Backend {
namespace {
auto Accepted(PeerHandle accepted) -> PeerHandle {
  Expects(accepted != nullptr, "accepted peer exists");
  return accepted;
}
auto OpenChannelManager(rdpContext* context) -> ChannelManager {
  // FreeRDP 3.15 WTSOpenServerA takes the peer's rdpContext through its server-name parameter.
  auto* opened = WTSOpenServerA(reinterpret_cast<char*>(context));
  if (!opened || opened == INVALID_HANDLE_VALUE) throw std::runtime_error("Channel manager allocation failed.");
  return ChannelManager{ opened };
}
}
PeerLink::PeerLink(PeerHandle accepted)
    : _client{ Accepted(std::move(accepted)) }, _socket{ _client->sockfd },
      _wake{ CreateEvent(nullptr, TRUE, FALSE, nullptr) } {
  if (!_wake) throw std::runtime_error("peer event allocation failed");
  if (!freerdp_peer_context_new(_client.get())) throw std::runtime_error("peer context failed");
  _channels = OpenChannelManager(_client->context);
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
auto PeerLink::Channels() const noexcept -> HANDLE {
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
auto PeerLink::Wake() const noexcept -> HANDLE {
  return _wake.get();
}
auto PeerLink::Invalidate() noexcept -> void {
  _handle_count = 0;
}
auto PeerLink::Refuse(UINT32 reason) -> void {
  freerdp_set_error_info(_client->context->rdp, reason);
  freerdp_send_error_info(_client->context->rdp);
}
auto PeerLink::Close() -> void {
  _client->Close(_client.get());
}
auto DynamicChannelsReady(PeerLink const& link) -> bool {
  return WTSVirtualChannelManagerGetDrdynvcState(link.Channels()) == DRDYNVC_STATE_READY;
}
auto Joined(PeerLink const& link, char const* name) -> bool {
  return WTSVirtualChannelManagerIsChannelJoined(link.Channels(), name);
}
}
