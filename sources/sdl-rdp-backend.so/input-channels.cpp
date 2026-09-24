#include "_detail/input.hpp"
#include "_detail/state.hpp"

#include <freerdp/channels/wtsvc.h>
#include <new>

namespace Backend {
Input& Input::Held(Peer& peer) {
  Expects(peer.client != nullptr, "peer owns its transport");
  Expects(peer.client->context, "peer transport has a context");
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): FreeRDP uses our ContextSize.
  auto* context = static_cast<InputContext*>(peer.client->context);
  Expects(context->state != nullptr, "input state exists");
  return *context->state;
}
BOOL Input::Create(freerdp_peer* /*unused*/, rdpContext* context) {
  Expects(context != nullptr, "context exists");
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): FreeRDP uses our ContextSize.
  auto* extended = static_cast<InputContext*>(context);
  extended->state = new (std::nothrow) Input;
  return extended->state != nullptr;
}
void Input::Free(freerdp_peer* /*unused*/, rdpContext* context) {
  Expects(context != nullptr, "context exists");
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast): FreeRDP uses our ContextSize.
  delete static_cast<InputContext*>(context)->state;
}

void Input::InstallChannels(Peer& peer) {
  advanced->data              = &peer;
  advanced->rdpcontext        = peer.client->context;
  advanced->MouseEvent        = Advanced;
  advanced->ChannelIdAssigned = [](ainput_server_context* context, UINT32 id) -> BOOL {
    Held(*static_cast<Peer*>(context->data)).advanced_id = id;
    return TRUE;
  };
  touch->user_data            = &peer;
  touch->onTouchEvent         = Touch;
  touch->onChannelIdAssigned  = [](RdpeiServerContext* context, UINT32 id) -> BOOL {
    Held(*static_cast<Peer*>(context->user_data)).touch_id = id;
    return TRUE;
  };
}
bool Input::Open(Peer& peer) {
  Expects(peer.channels != nullptr, "channel manager exists");
  opened            = true;
  peer.handle_count = 0;
  advanced.reset(ainput_server_context_new(peer.channels));
  touch.reset(rdpei_server_context_new(peer.channels));
  if (!advanced || !touch) return false;
  InstallChannels(peer);
  return advanced->Initialize(advanced.get(), TRUE) == CHANNEL_RC_OK &&
         advanced->Open(advanced.get()) == CHANNEL_RC_OK && advanced->Poll(advanced.get()) == CHANNEL_RC_OK &&
         advanced->ChannelHandle(advanced.get(), &advanced_event) && rdpei_server_init(touch.get()) == CHANNEL_RC_OK;
}
bool Input::Channels(Peer& peer, std::span<HANDLE const> ready) {
  Expects(peer.channels != nullptr, "channel manager exists");
  if (WTSVirtualChannelManagerGetDrdynvcState(peer.channels) != DRDYNVC_STATE_READY) return true;
  if (!opened) return Open(peer);
  if (advanced_ready && std::ranges::contains(ready, advanced_event) && advanced->Poll(advanced.get()) != CHANNEL_RC_OK)
    return false;
  if (touch_ready && std::ranges::contains(ready, rdpei_server_get_event_handle(touch.get()))) {
    auto result = rdpei_server_handle_messages(touch.get());
    // FreeRDP 3.15 channels/rdpei/server/rdpei_main.c:701 maps ERROR_NO_DATA to ERROR_READ_FAULT.
    if (result != CHANNEL_RC_OK && result != ERROR_READ_FAULT) return false;
  }
  return true;
}
unsigned Input::Handles(HANDLE* handles) const {
  Expects(handles != nullptr, "space for two channel handles exists");
  unsigned count = 0;
  if (advanced_ready) handles[count++] = advanced_event;
  if (touch_ready) handles[count++] = rdpei_server_get_event_handle(touch.get());
  return count;
}
void Input::Close() {
  advanced_event = nullptr;
  advanced.reset();
  touch.reset();
}
} // namespace Backend
