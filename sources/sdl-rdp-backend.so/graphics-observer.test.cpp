#include "_detail/graphics-observer.hpp"

#include <freerdp/codec/h264.h>
#include <freerdp/gdi/gdi.h>
#include <string_view>

namespace Headless {
GraphicsObserver::GraphicsObserver(Client& target)
    : client(target), desktop_resize(client.Instance()->context->update->DesktopResize) {
  Expects(!active, "one graphics observer per thread");
  active = this;

  client.Instance()->context->update->DesktopResize = [](rdpContext* context) -> BOOL {
    active->observed.desktops.emplace_back(freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth),
                                           freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight));
    return active->desktop_resize(context);
  };
  PubSub_SubscribeChannelConnected(client.Instance()->context->pubSub, Connected);
}
GraphicsObserver::~GraphicsObserver() {
  freerdp_disconnect(client.Instance().get());
  PubSub_UnsubscribeChannelConnected(client.Instance()->context->pubSub, Connected);
  client.Instance()->context->update->DesktopResize = desktop_resize;
  active                                            = nullptr;
}
bool GraphicsObserver::Ack(UINT32 depth) {
  Expects(channel, "channel is installed");
  Expects(!observed.frames.empty(), "observer has received a frame");
  return AckFrame(observed.frames.size() - 1, depth);
}
bool GraphicsObserver::AckFrame(std::size_t index, UINT32 depth) {
  Expects(channel, "channel is installed");
  Expects(index < observed.frames.size(), "frame index is in range");
  auto ack = observed.frames[index];
  ack.queueDepth = depth;
  return original(channel, &ack) == CHANNEL_RC_OK;
}
RdpgfxClientContext* GraphicsObserver::Channel() const {
  return channel;
}
GraphicsCapture& GraphicsObserver::Observed() {
  return observed;
}
GraphicsCapture const& GraphicsObserver::Observed() const {
  return observed;
}
void GraphicsObserver::Connected(void* /*unused*/, ChannelConnectedEventArgs const* event) {
  if (std::string_view(event->name) != RDPGFX_DVC_CHANNEL_NAME) return;
  active->channel                = static_cast<RdpgfxClientContext*>(event->pInterface);
  active->create                 = active->channel->CreateSurface;
  active->remove                 = active->channel->DeleteSurface;
  active->channel->CreateSurface = [](RdpgfxClientContext* channel, RDPGFX_CREATE_SURFACE_PDU const* surface) -> UINT {
    active->observed.surfaces.push_back(*surface);
    return active->create(channel, surface);
  };
  active->channel->DeleteSurface = [](RdpgfxClientContext* channel, RDPGFX_DELETE_SURFACE_PDU const* surface) -> UINT {
    if (channel->GetSurfaceData(channel, surface->surfaceId)) ++active->observed.deleted;
    return active->remove(channel, surface);
  };
  active->ObserveFrameLifecycle();
}
void GraphicsObserver::ObserveAvc(RDPGFX_SURFACE_COMMAND const& command) {
  Expects(command.extra, "AVC command has a parsed bitmap stream");
  auto const& stream = *static_cast<RDPGFX_AVC420_BITMAP_STREAM const*>(command.extra);
  observed.avc_rects.assign(stream.meta.regionRects, stream.meta.regionRects + stream.meta.numRegionRects);
  observed.avc_quality.assign(stream.meta.quantQualityVals, stream.meta.quantQualityVals + stream.meta.numRegionRects);
  unsigned types = 0;
  for (std::size_t i = 0; i + 3 < stream.length; ++i)
    if (!stream.data[i] && !stream.data[i + 1] && stream.data[i + 2] == 1) types |= 1u << (stream.data[i + 3] & 31);
  observed.avc_nals.push_back(types);
}
void GraphicsObserver::ObserveFrameLifecycle() {
  Expects(channel != nullptr, "graphics channel connected");
  surface                 = channel->SurfaceCommand;
  channel->SurfaceCommand = [](RdpgfxClientContext* channel, RDPGFX_SURFACE_COMMAND const* command) -> UINT {
    ++active->observed.commands;
    if (command->codecId == RDPGFX_CODECID_AVC420) active->ObserveAvc(*command);
    if (command->codecId == RDPGFX_CODECID_CAPROGRESSIVE && command->length >= 2 && command->data[0] == 0xc0 &&
        command->data[1] == 0xcc)
      ++active->observed.progressive_headers;
    return active->surface(channel, command);
  };
  reset                   = channel->ResetGraphics;
  channel->ResetGraphics  = [](RdpgfxClientContext* channel, RDPGFX_RESET_GRAPHICS_PDU const* reset) -> UINT {
    active->observed.resets.push_back({ reset->width,
                                        reset->height,
                                        { reset->monitorDefArray, reset->monitorDefArray + reset->monitorCount },
                                        active->observed.desktops.size(),
                                        active->observed.frames.size() });
    return active->reset(channel, reset);
  };
  ObserveFrames();
}
void GraphicsObserver::ObserveFrames() {
  original          = channel->FrameAcknowledge;
  end               = channel->EndFrame;
  channel->OnOpen   = [](RdpgfxClientContext*, BOOL* send_caps, BOOL* send_acks) -> UINT {
    *send_caps = active->observed.advertise;
    *send_acks = FALSE;
    return CHANNEL_RC_OK;
  };
  channel->EndFrame = [](RdpgfxClientContext* channel, RDPGFX_END_FRAME_PDU const* frame) -> UINT {
    auto result = active->end(channel, frame);
    if (result != CHANNEL_RC_OK) return result;
    RDPGFX_FRAME_ACKNOWLEDGE_PDU const ack{ 0, frame->frameId, UINT32(active->observed.frames.size() + 1) };
    active->observed.frames.push_back(ack);
    return active->observed.automatic ? active->original(channel, &ack) : CHANNEL_RC_OK;
  };
}
}
