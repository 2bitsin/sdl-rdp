#include "_detail/peer-link.hpp"

#include "_detail/contract.hpp"

#include <freerdp/channels/wtsvc.h>
#include <stdexcept>
#include <utility>
#include <winpr/synch.h>

namespace Backend {
namespace {
PeerHandle Accepted(PeerHandle accepted) {
  Expects(accepted != nullptr, "accepted peer exists");
  return accepted;
}
ChannelManager OpenChannelManager(rdpContext* context) {
  auto* opened = WTSOpenServerA(reinterpret_cast<char*>(context));
  if (!opened || opened == INVALID_HANDLE_VALUE) throw std::runtime_error("Channel manager allocation failed.");
  return ChannelManager{ opened };
}
}
PeerLink::PeerLink(PeerHandle accepted)
    : _client { Accepted(std::move(accepted)) }, _socket{ _client->sockfd },
      _wake{ CreateEvent(nullptr, TRUE, FALSE, nullptr) } {
  if (!_wake) throw std::runtime_error("peer event allocation failed");
  if (!freerdp_peer_context_new(_client.get())) throw std::runtime_error("peer context failed");
  _channels = OpenChannelManager(_client->context);
}
freerdp_peer& PeerLink::Client() const noexcept {
  return *_client;
}
rdpContext& PeerLink::Context() const noexcept {
  return *_client->context;
}
rdpSettings& PeerLink::Settings() const noexcept {
  return *_client->context->settings;
}
HANDLE PeerLink::Channels() const noexcept {
  return _channels.get();
}
int PeerLink::Socket() const noexcept {
  return _socket;
}
bool PeerLink::WriteBlocked() const {
  return _client->IsWriteBlocked(_client.get());
}
void PeerLink::Signal() {
  _wake.Transition(WakeEvent::Phase::Pending);
}
void PeerLink::Settle() {
  _wake.Transition(WakeEvent::Phase::Idle);
}
HANDLE PeerLink::Wake() const noexcept {
  return _wake.get();
}
void PeerLink::Invalidate() noexcept {
  _handle_count = 0;
}
void PeerLink::Refuse(UINT32 reason) {
  freerdp_set_error_info(_client->context->rdp, reason);
  freerdp_send_error_info(_client->context->rdp);
}
void PeerLink::Close() {
  _client->Close(_client.get());
}
bool DynamicChannelsReady(PeerLink const& link) {
  return WTSVirtualChannelManagerGetDrdynvcState(link.Channels()) == DRDYNVC_STATE_READY;
}
bool Joined(PeerLink const& link, char const* name) {
  return WTSVirtualChannelManagerIsChannelJoined(link.Channels(), name);
}
}
