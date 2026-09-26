#include <sdl-rdp/headless-client.test/graphics/observer.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/codec/h264.h>
#include <freerdp/gdi/gdi.h>
#include <sdl-rdp/headless-client.test/client/handles.hpp>
#include <cstddef>
#include <cstdint>

namespace sdl_rdp::headless_client_test::graphics::detail::observer {
using sdl_rdp::headless_client_test::client::ClientContext;
using sdl_rdp::headless_client_test::client::ClientUpdates;
using sdl_rdp::headless_client_test::utilities::Delegated;
using sdl_rdp::headless_client_test::utilities::ObserverLease;
using sdl_rdp::headless_client_test::utilities::ObserverSet;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Required;

namespace {
auto Held(rdpContext& context) -> ObserverLease<GraphicsObserver> {
  return ObserverSet::Of(context).Held<GraphicsObserver>();
}
auto Held(RdpgfxClientContext& channel) -> ObserverLease<GraphicsObserver> {
  Expects(channel.custom != nullptr, "the graphics channel carries its decoder");
  auto const& decoder = *static_cast<rdpGdi*>(channel.custom);
  Expects(decoder.context != nullptr, "the decoder names its client context");
  return Held(*decoder.context);
}
}
GraphicsObserver::GraphicsObserver(Client& target)
    : client(target), desktop_resize(ClientUpdates(client).DesktopResize), membership(ClientContext(client), *this),
      connections(ClientContext(client)) {
  ClientUpdates(client).DesktopResize = Delegated<&GraphicsObserver::Resize>;
}
GraphicsObserver::~GraphicsObserver() {
  client.Disconnect();
  ClientUpdates(client).DesktopResize = desktop_resize;
}
auto GraphicsObserver::Resize(rdpContext& context) -> bool {
  observed.desktops.emplace_back(freerdp_settings_get_uint32(context.settings, FreeRDP_DesktopWidth),
                                 freerdp_settings_get_uint32(context.settings, FreeRDP_DesktopHeight));
  return desktop_resize(&context);
}
auto GraphicsObserver::Ack(std::uint32_t depth) -> bool {
  Expects(channel.has_value(), "channel is installed");
  Expects(!observed.frames.empty(), "observer has received a frame");
  return AckFrame(observed.frames.size() - 1, depth);
}
auto GraphicsObserver::AckFrame(std::size_t index, std::uint32_t depth) -> bool {
  Expects(index < observed.frames.size(), "frame index is in range");
  auto ack = observed.frames[index];
  ack.queueDepth = depth;
  return original(&Channel(), &ack) == CHANNEL_RC_OK;
}
auto GraphicsObserver::Channel() const -> RdpgfxClientContext& {
  return Required(channel, "channel is installed").get();
}
auto GraphicsObserver::Observed() -> GraphicsCapture& {
  return observed;
}
auto GraphicsObserver::Observed() const -> GraphicsCapture const& {
  return observed;
}
auto GraphicsObserver::Attach(RdpgfxClientContext& context) -> void {
  channel = context;
  create  = context.CreateSurface;
  remove  = context.DeleteSurface;
  // abi: pcRdpgfxCreateSurface and pcRdpgfxDeleteSurface, UINT is uint32_t
  context.CreateSurface = [](RdpgfxClientContext* channel, RDPGFX_CREATE_SURFACE_PDU const* surface) -> std::uint32_t {
    Expects(channel != nullptr, "the graphics callback names its channel");
    Expects(surface != nullptr, "created surface is supplied");
    auto const self = Held(*channel);
    self->observed.surfaces.push_back(*surface);
    return self->create(channel, surface);
  };
  context.DeleteSurface = [](RdpgfxClientContext* channel, RDPGFX_DELETE_SURFACE_PDU const* surface) -> std::uint32_t {
    Expects(channel != nullptr, "the graphics callback names its channel");
    Expects(surface != nullptr, "deleted surface is supplied");
    auto const self = Held(*channel);
    if (channel->GetSurfaceData(channel, surface->surfaceId)) ++self->observed.deleted;
    return self->remove(channel, surface);
  };
  ObserveFrameLifecycle();
}
auto GraphicsObserver::ObserveAvc(RDPGFX_SURFACE_COMMAND const& command) -> void {
  Expects(command.extra, "AVC command has a parsed bitmap stream");
  auto const& stream = *static_cast<RDPGFX_AVC420_BITMAP_STREAM const*>(command.extra);
  observed.avc_rects.assign(stream.meta.regionRects, stream.meta.regionRects + stream.meta.numRegionRects);
  observed.avc_quality.assign(stream.meta.quantQualityVals, stream.meta.quantQualityVals + stream.meta.numRegionRects);
  std::uint32_t types = 0;
  for (std::size_t i = 0; i + 3 < stream.length; ++i)
    if (!stream.data[i] && !stream.data[i + 1] && stream.data[i + 2] == 1) types |= 1u << (stream.data[i + 3] & 31u);
  observed.avc_nals.push_back(types);
}
auto GraphicsObserver::ObserveFrameLifecycle() -> void {
  auto& connected = Channel();
  surface                  = connected.SurfaceCommand;
  connected.SurfaceCommand = [](RdpgfxClientContext* channel, RDPGFX_SURFACE_COMMAND const* command) -> std::uint32_t {
    Expects(channel != nullptr, "the graphics callback names its channel");
    Expects(command != nullptr, "surface command is supplied");
    auto const self = Held(*channel);
    ++self->observed.commands;
    if (command->codecId == RDPGFX_CODECID_AVC420) self->ObserveAvc(*command);
    if (command->codecId == RDPGFX_CODECID_CAPROGRESSIVE && command->length >= 2 && command->data[0] == 0xc0
        && command->data[1] == 0xcc)
      ++self->observed.progressive_headers;
    return self->observed.decode ? self->surface(channel, command) : CHANNEL_RC_OK;
  };
  ObserveResets();
}
auto GraphicsObserver::ObserveResets() -> void {
  auto& connected = Channel();
  reset = connected.ResetGraphics;
  // abi: pcRdpgfxResetGraphics, UINT is uint32_t
  connected.ResetGraphics = [](RdpgfxClientContext* channel, RDPGFX_RESET_GRAPHICS_PDU const* reset) -> std::uint32_t {
    Expects(channel != nullptr, "the graphics callback names its channel");
    Expects(reset != nullptr, "graphics reset is supplied");
    auto const self = Held(*channel);
    self->observed.resets.push_back({ reset->width,
                                      reset->height,
                                      { reset->monitorDefArray, reset->monitorDefArray + reset->monitorCount },
                                      self->observed.desktops.size(),
                                      self->observed.frames.size() });
    return self->reset(channel, reset);
  };
  ObserveFrames();
}
auto GraphicsObserver::ObserveFrames() -> void {
  auto& connected = Channel();
  original = connected.FrameAcknowledge;
  // abi: pcRdpgfxOnOpen, BOOL is int
  connected.OnOpen = [](RdpgfxClientContext* opened, int* send_caps, int* send_acks) -> std::uint32_t {
    Expects(opened != nullptr, "the graphics callback names its channel");
    Expects(send_caps != nullptr, "capability flag is supplied");
    Expects(send_acks != nullptr, "acknowledgement flag is supplied");
    *send_caps = Held(*opened)->observed.advertise;
    *send_acks = false;
    return CHANNEL_RC_OK;
  };
  ObserveEndFrames(connected);
}
auto GraphicsObserver::ObserveEndFrames(RdpgfxClientContext& connected) -> void {
  end = connected.EndFrame;
  // abi: pcRdpgfxEndFrame, UINT is uint32_t
  connected.EndFrame = [](RdpgfxClientContext* channel, RDPGFX_END_FRAME_PDU const* frame) -> std::uint32_t {
    Expects(channel != nullptr, "the graphics callback names its channel");
    Expects(frame != nullptr, "ended frame is supplied");
    auto const self   = Held(*channel);
    auto       result = self->end(channel, frame);
    if (result != CHANNEL_RC_OK) return result;
    RDPGFX_FRAME_ACKNOWLEDGE_PDU const ack{ 0, frame->frameId,
                                            Narrowed<std::uint32_t>(self->observed.frames.size() + 1) };
    self->observed.frames.push_back(ack);
    return self->observed.automatic ? self->original(channel, &ack) : CHANNEL_RC_OK;
  };
}
}
