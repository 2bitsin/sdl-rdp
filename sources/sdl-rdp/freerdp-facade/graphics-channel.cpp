#include <sdl-rdp/freerdp-facade/graphics-channel.hpp>

#include <sdl-rdp/freerdp-facade/assignment-sink.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/freerdp-facade/lent.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>

#include <freerdp/codec/color.h>
#include <freerdp/server/rdpgfx.h>
#include <oxbox/utilities/span.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::graphics_channel {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::OperationName;
using sdl_rdp::utilities::Unreachable;

namespace {
constexpr OperationName GraphicsCapabilities    { "Graphics capabilities"          };
constexpr OperationName GraphicsAcknowledgement { "Graphics frame acknowledgement" };
constexpr OperationName GraphicsQoe             { "Graphics QoE acknowledgement"   };
constexpr OperationName GraphicsAssignment      { "Graphics channel assignment"    };
constexpr std::uint32_t Version101DataLength    = 16;
constexpr std::uint32_t FlagsDataLength         = 4;
constexpr auto          UserData                = &RdpgfxServerContext::custom;
using Owner = GraphicsChannelEvents;
auto Events(RdpgfxServerContext const& context) -> GraphicsChannelEvents& {
  return CallbackOwner<Owner, UserData>(context);
}
// MS-RDPEGFX 2.2.3.4: version 10.1 carries 16 reserved bytes where every other version carries its flags.
auto DataLength(GfxVersion version) -> std::uint32_t {
  return version == GfxVersion::V101 ? Version101DataLength : FlagsDataLength;
}
auto WellFormed(RDPGFX_CAPSET const& set) -> bool {
  return set.length >= DataLength(GfxVersion{ set.version });
}
auto Capability(RDPGFX_CAPSET const& set) -> GfxCapability {
  return { .version = GfxVersion{ set.version }, .flags = GfxCapsFlags{ set.flags } };
}
auto Advertised(RDPGFX_CAPS_ADVERTISE_PDU const& pdu) -> std::vector<GfxCapability> {
  return std::span(pdu.capsSets, pdu.capsSetCount) | std::views::filter(WellFormed) | std::views::transform(Capability)
         | std::ranges::to<std::vector>();
}
auto CodecId(GfxCodec codec) -> std::uint16_t {
  switch (codec) {
  case GfxCodec::Uncompressed: return RDPGFX_CODECID_UNCOMPRESSED;
  case GfxCodec::Planar:       return RDPGFX_CODECID_PLANAR;
  case GfxCodec::Progressive:  return RDPGFX_CODECID_CAPROGRESSIVE;
  case GfxCodec::Avc420:       return RDPGFX_CODECID_AVC420;
  default:                     Unreachable(codec);
  }
}
// MS-RDPEGFX 2.2.2.2 corners are exclusive.
auto Corners(Rect area) -> RECTANGLE_16 {
  return { .left   = Narrowed<std::uint16_t>(area.x),
           .top    = Narrowed<std::uint16_t>(area.y),
           .right  = Narrowed<std::uint16_t>(area.x + area.w),
           .bottom = Narrowed<std::uint16_t>(area.y + area.h) };
}
// MS-RDPBCGR 2.2.1.3.6.1 monitor corners are inclusive.
auto Monitor(GraphicsMonitor const& monitor) -> MONITOR_DEF {
  auto const area = monitor.area;
  return { .left   = area.x,
           .top    = area.y,
           .right  = area.x + area.w - 1,
           .bottom = area.y + area.h - 1,
           .flags  = monitor.primary ? std::uint32_t{ MONITOR_PRIMARY } : 0 };
}
auto Quality(QuantQuality quality) -> RDPGFX_H264_QUANT_QUALITY {
  constexpr std::uint8_t ProgressiveBit = 7;
  auto const             progressive    = std::uint8_t{ quality.progressive };
  return { .qpVal      = Narrowed<std::uint8_t>(quality.qp | (progressive << ProgressiveBit)),
           .qualityVal = quality.quality,
           .qp         = quality.qp,
           .r          = 0,
           .p          = progressive };
}
auto Command(GraphicsCommand const& command, std::span<std::uint8_t const> payload) -> RDPGFX_SURFACE_COMMAND {
  auto const area   = command.area;
  auto       result = RDPGFX_SURFACE_COMMAND{ };
  result.surfaceId = command.surface;
  result.codecId   = CodecId(command.codec);
  result.contextId = command.context;
  result.format    = PIXEL_FORMAT_BGRX32;
  result.left      = Narrowed<std::uint32_t>(area.x);
  result.top       = Narrowed<std::uint32_t>(area.y);
  result.right     = Narrowed<std::uint32_t>(area.x + area.w);
  result.bottom    = Narrowed<std::uint32_t>(area.y + area.h);
  result.width     = Narrowed<std::uint32_t>(area.w);
  result.height    = Narrowed<std::uint32_t>(area.h);
  result.length    = Narrowed<std::uint32_t>(payload.size());
  result.data      = Lent(payload).data();
  return result;
}
// A refused advertisement has no acceptable capability; a reply that could not be sent is its verb's own false.
auto Answered(bool accepted) -> std::uint32_t {
  return accepted ? CHANNEL_RC_OK : ERROR_NOT_SUPPORTED;
}
auto Sent(std::uint32_t code) -> bool {
  return code == CHANNEL_RC_OK;
}
auto Caps(GraphicsChannelEvents& events, RDPGFX_CAPS_ADVERTISE_PDU const& pdu) -> std::uint32_t {
  return Answered(events.CapsAdvertise(Advertised(pdu)));
}
auto Ack(GraphicsChannelEvents& events, RDPGFX_FRAME_ACKNOWLEDGE_PDU const& pdu) -> std::uint32_t {
  auto const suspended = pdu.queueDepth == SUSPEND_FRAME_ACKNOWLEDGEMENT;
  events.FrameAcknowledge(
      { .frame = pdu.frameId, .queue_depth = suspended ? 0 : pdu.queueDepth, .suspended = suspended });
  return CHANNEL_RC_OK;
}
auto Qoe(GraphicsChannelEvents& events, RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const& pdu) -> std::uint32_t {
  events.QoeFrameAcknowledge({ .frame         = pdu.frameId,
                               .timestamp     = pdu.timestamp,
                               .time_diff_se  = pdu.timeDiffSE,
                               .time_diff_edr = pdu.timeDiffEDR });
  return CHANNEL_RC_OK;
}
auto InstallSlots(RdpgfxServerContext& context) -> void {
  constexpr auto failed = ERROR_INTERNAL_ERROR;
  // abi: psRdpgfxCapsAdvertise, FrameAcknowledge, QoeFrameAcknowledge, UINT is uint32_t; ChannelIdAssigned, BOOL is int
  context.CapsAdvertise       = Handled<Events, Caps, GraphicsCapabilities, SinkFailures, failed>;
  context.FrameAcknowledge    = Handled<Events, Ack, GraphicsAcknowledgement, SinkFailures, failed>;
  context.QoeFrameAcknowledge = Handled<Events, Qoe, GraphicsQoe, SinkFailures, failed>;
  context.ChannelIdAssigned   = Handled<Events, Assigned, GraphicsAssignment, SinkFailures, false>;
}
}
auto ReleaseGraphics(s_rdpgfx_server_context* context) noexcept -> void {
  rdpgfx_server_context_free(context);
}

class GraphicsChannel::Avc420Buffers {
public:
  auto Stream(Avc420Metablock const& metablock, std::span<std::uint8_t const> payload) -> RDPGFX_AVC420_BITMAP_STREAM;

private:
  std::vector<RECTANGLE_16>              _regions;
  std::vector<RDPGFX_H264_QUANT_QUALITY> _quality;
};
auto GraphicsChannel::Avc420Buffers::Stream(Avc420Metablock const& metablock, std::span<std::uint8_t const> payload)
    -> RDPGFX_AVC420_BITMAP_STREAM {
  _regions.assign_range(metablock.regions | std::views::transform(Corners));
  _quality.assign(_regions.size(), Quality(metablock.quality));
  return { .meta   = { .numRegionRects   = Narrowed<std::uint32_t>(_regions.size()),
                       .regionRects      = _regions.data(),
                       .quantQualityVals = _quality.data() },
           .length = Narrowed<std::uint32_t>(payload.size()),
           .data   = Lent(payload).data() };
}
GraphicsChannel::GraphicsChannel(ChannelManager& channels, GraphicsChannelEvents& events) noexcept
    : _channels{ channels }, _events{ events } { }
GraphicsChannel::~GraphicsChannel() = default;
auto GraphicsChannel::Open() -> bool {
  Expects(_context == nullptr, "graphics opens once");
  _context = _channels.Bound<GraphicsContext, rdpgfx_server_context_new, UserData, InstallSlots, Owner>(_events);
  _avc420  = std::make_unique<Avc420Buffers>();
  auto& context = *_context;
  return context.Initialize(&context, true) && context.Open(&context);
}
auto GraphicsChannel::Pump() -> bool {
  auto const code = rdpgfx_server_handle_messages(&Context());
  return code == CHANNEL_RC_OK || code == ERROR_NO_DATA;
}
auto GraphicsChannel::Handle() const -> WaitHandle {
  return WaitHandle::Lent<rdpgfx_server_get_event_handle>(Context());
}
auto GraphicsChannel::CapsConfirm(GfxCapability capability) -> bool {
  RDPGFX_CAPSET                 set     { .version = std::to_underlying(capability.version),
                                          .length  = DataLength(capability.version),
                                          .flags   = std::to_underlying(capability.flags) };
  RDPGFX_CAPS_CONFIRM_PDU const confirm { &set };
  auto&                         context = Context();
  return Sent(context.CapsConfirm(&context, &confirm));
}
auto GraphicsChannel::ResetGraphics(Extent desktop, std::span<GraphicsMonitor const> monitors) -> bool {
  auto                            defined = monitors | std::views::transform(Monitor) | std::ranges::to<std::vector>();
  RDPGFX_RESET_GRAPHICS_PDU const reset   { .width           = desktop.width,
                                            .height          = desktop.height,
                                            .monitorCount    = Narrowed<std::uint32_t>(defined.size()),
                                            .monitorDefArray = defined.data() };
  auto&                           context = Context();
  return Sent(context.ResetGraphics(&context, &reset));
}
auto GraphicsChannel::CreateSurface(SurfaceSpec surface) -> bool {
  RDPGFX_CREATE_SURFACE_PDU const create  { .surfaceId   = surface.id,
                                            .width       = Narrowed<std::uint16_t>(surface.size.width),
                                            .height      = Narrowed<std::uint16_t>(surface.size.height),
                                            .pixelFormat = GFX_PIXEL_FORMAT_XRGB_8888 };
  auto&                           context = Context();
  return Sent(context.CreateSurface(&context, &create));
}
auto GraphicsChannel::DeleteSurface(std::uint16_t surface) -> bool {
  RDPGFX_DELETE_SURFACE_PDU const remove  { surface };
  auto&                           context = Context();
  return Sent(context.DeleteSurface(&context, &remove));
}
auto GraphicsChannel::MapSurfaceToOutput(std::uint16_t surface) -> bool {
  RDPGFX_MAP_SURFACE_TO_OUTPUT_PDU const map     {
    .surfaceId = surface, .reserved = 0, .outputOriginX = 0, .outputOriginY = 0
  };
  auto&                                  context = Context();
  return Sent(context.MapSurfaceToOutput(&context, &map));
}
auto GraphicsChannel::DeleteEncodingContext(std::uint16_t surface, std::uint32_t context_id) -> bool {
  RDPGFX_DELETE_ENCODING_CONTEXT_PDU const encoding { surface, context_id };
  auto&                                    context  = Context();
  return Sent(context.DeleteEncodingContext(&context, &encoding));
}
auto GraphicsChannel::StartFrame(std::uint32_t frame, std::chrono::system_clock::time_point at) -> bool {
  RDPGFX_START_FRAME_PDU const start   { .timestamp = FrameTimestamp(at), .frameId = frame };
  auto&                        context = Context();
  return Sent(context.StartFrame(&context, &start));
}
auto GraphicsChannel::EndFrame(std::uint32_t frame) -> bool {
  RDPGFX_END_FRAME_PDU const end     { frame };
  auto&                      context = Context();
  return Sent(context.EndFrame(&context, &end));
}
auto GraphicsChannel::SurfaceCommand(GraphicsCommand const& command) -> bool {
  auto const& regions = command.metablock.regions;
  bool const  avc     = command.codec == GfxCodec::Avc420;
  if (avc)
    Expects(!regions.empty(), "an AVC420 command carries its regions");
  else
    Expects(regions.empty(), "only an AVC420 command carries regions");
  auto&                                      context = Context();
  auto const                                 payload = oxbox::utilities::SpanCast<std::uint8_t const>(command.payload);
  auto                                       wire    = Command(command, payload);
  std::optional<RDPGFX_AVC420_BITMAP_STREAM> stream;
  if (avc) wire.extra = &stream.emplace(_avc420->Stream(command.metablock, payload));
  return Sent(context.SurfaceCommand(&context, &wire));
}
auto GraphicsChannel::Context() const -> s_rdpgfx_server_context& {
  Expects(_context != nullptr, "the graphics channel is open");
  return *_context;
}
auto FrameTimestamp(std::chrono::system_clock::time_point at) -> std::uint32_t {
  constexpr std::uint32_t     HourShift   = 22;
  constexpr std::uint32_t     MinuteShift = 16;
  constexpr std::uint32_t     SecondShift = 10;
  auto const                  day         = std::chrono::floor<std::chrono::days>(at);
  std::chrono::hh_mm_ss const time        { std::chrono::floor<std::chrono::milliseconds>(at - day) };
  return (Narrowed<std::uint32_t>(time.hours().count()) << HourShift)
         | (Narrowed<std::uint32_t>(time.minutes().count()) << MinuteShift)
         | (Narrowed<std::uint32_t>(time.seconds().count()) << SecondShift)
         | Narrowed<std::uint32_t>(time.subseconds().count());
}
}
