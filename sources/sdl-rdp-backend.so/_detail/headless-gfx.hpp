#pragma once
#include "headless-client.hpp"

namespace Headless {
class GraphicsObserver {
  inline static thread_local GraphicsObserver* active = nullptr;
  Client& client;
  pcRdpgfxFrameAcknowledge original = nullptr;
  pcRdpgfxEndFrame end = nullptr;
  pcRdpgfxSurfaceCommand surface = nullptr;
  pcRdpgfxCreateSurface create = nullptr;
  pcRdpgfxDeleteSurface remove = nullptr;
public:
  RdpgfxClientContext* channel = nullptr;
  std::vector<RDPGFX_FRAME_ACKNOWLEDGE_PDU> frames;
  bool automatic = true, advertise = true;
  std::vector<RDPGFX_CREATE_SURFACE_PDU> surfaces;
  unsigned deleted = 0, progressive_headers = 0, commands = 0;
  explicit GraphicsObserver(Client& target) : client(target) {
    Expects(!active, "one graphics observer per thread");
    active = this;
    PubSub_SubscribeChannelConnected(client.instance->context->pubSub, Connected);
  }
  ~GraphicsObserver() {
    freerdp_disconnect(client.instance.get());
    PubSub_UnsubscribeChannelConnected(client.instance->context->pubSub, Connected);
    active = nullptr;
  }
  static void Connected(void*, ChannelConnectedEventArgs const* event) {
    if (std::string_view(event->name) != RDPGFX_DVC_CHANNEL_NAME) return;
    active->channel = static_cast<RdpgfxClientContext*>(event->pInterface);
    active->create = active->channel->CreateSurface;
    active->remove = active->channel->DeleteSurface;
    active->channel->CreateSurface = [](RdpgfxClientContext* channel, RDPGFX_CREATE_SURFACE_PDU const* surface) -> UINT {
      active->surfaces.push_back(*surface);
      return active->create(channel, surface);
    };
    active->channel->DeleteSurface = [](RdpgfxClientContext* channel, RDPGFX_DELETE_SURFACE_PDU const* surface) -> UINT {
      if (channel->GetSurfaceData(channel, surface->surfaceId)) ++active->deleted;
      return active->remove(channel, surface);
    };
    active->surface = active->channel->SurfaceCommand;
    active->channel->SurfaceCommand = [](RdpgfxClientContext* channel, RDPGFX_SURFACE_COMMAND const* command) -> UINT {
      ++active->commands;
      if (command->codecId == RDPGFX_CODECID_CAPROGRESSIVE && command->length >= 2
          && command->data[0] == 0xc0 && command->data[1] == 0xcc) ++active->progressive_headers;
      return active->surface(channel, command);
    };
    active->ObserveFrames();
  }
  void ObserveFrames() {
    original = channel->FrameAcknowledge;
    end = channel->EndFrame;
    channel->OnOpen = [](RdpgfxClientContext*, BOOL* send_caps, BOOL* send_acks) -> UINT {
      *send_caps = active->advertise;
      *send_acks = FALSE;
      return CHANNEL_RC_OK;
    };
    channel->EndFrame = [](RdpgfxClientContext* channel, RDPGFX_END_FRAME_PDU const* frame) -> UINT {
      auto result = active->end(channel, frame);
      if (result != CHANNEL_RC_OK) return result;
      RDPGFX_FRAME_ACKNOWLEDGE_PDU ack{0, frame->frameId, UINT32(active->frames.size() + 1)};
      active->frames.push_back(ack);
      return active->automatic ? active->original(channel, &ack) : CHANNEL_RC_OK;
    };
  }
  bool Ack(UINT32 depth = 0) {
    Expects(channel && !frames.empty(), "decoded frame available to acknowledge");
    return AckFrame(frames.size() - 1, depth);
  }
  bool AckFrame(std::size_t index, UINT32 depth) {
    Expects(channel && index < frames.size(), "acknowledged frame was decoded");
    auto ack = frames[index];
    ack.queueDepth = depth;
    return original(channel, &ack) == CHANNEL_RC_OK;
  }
};
}
