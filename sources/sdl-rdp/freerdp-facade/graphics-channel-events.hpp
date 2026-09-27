#pragma once
#include <sdl-rdp/freerdp-facade/assignment-sink.hpp>
#include <sdl-rdp/utilities/flags.hpp>

#include <cstdint>
#include <span>
#include <type_traits>

namespace sdl_rdp::freerdp_facade::detail::graphics_channel_events {
using sdl_rdp::utilities::Has;
using sdl_rdp::utilities::operator&;
using sdl_rdp::utilities::operator|;

// The capability versions of MS-RDPEGFX 2.2.3; a client may advertise any other value.
enum class GfxVersion : std::uint32_t {
  V8      = 0x00080004,
  V81     = 0x00080105,
  V10     = 0x000A0002,
  V101    = 0x000A0100,
  V102    = 0x000A0200,
  V103    = 0x000A0301,
  V104    = 0x000A0400,
  V105    = 0x000A0502,
  V106    = 0x000A0600,
  V106Err = 0x000A0601,
  V107    = 0x000A0701,
};
// The capability flags of MS-RDPEGFX 2.2.3; a client may set any other bit.
enum class GfxCapsFlags : std::uint32_t {
  ThinClient       = 0x01,
  SmallCache       = 0x02,
  Avc420Enabled    = 0x10,
  AvcDisabled      = 0x20,
  AvcThinClient    = 0x40,
  ScaledMapDisable = 0x80,
};
auto FlagSet(GfxCapsFlags /*set*/) -> std::true_type;
struct GfxCapability {
  GfxVersion   version{ };
  GfxCapsFlags flags  { };
};
// A queue depth is the client's backlog in bytes, zero when it does not report one (MS-RDPEGFX 2.2.2.13).
struct FrameAck {
  std::uint32_t frame      { };
  std::uint32_t queue_depth{ };
  bool          suspended  { };
};
struct QoeAck {
  std::uint32_t frame        { };
  std::uint32_t timestamp    { };
  std::uint16_t time_diff_se { };
  std::uint16_t time_diff_edr{ };
};
// What a graphics channel's client PDUs report. CapsAdvertise's false: the advertisement was not confirmed.
class GraphicsChannelEvents : public AssignmentSink {
public:
  virtual auto CapsAdvertise(std::span<GfxCapability const> advertised) -> bool = 0;
  virtual auto FrameAcknowledge(FrameAck ack)                           -> void = 0;
  virtual auto QoeFrameAcknowledge(QoeAck ack)                          -> void = 0;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::graphics_channel_events::FrameAck;
using detail::graphics_channel_events::GfxCapability;
using detail::graphics_channel_events::GfxCapsFlags;
using detail::graphics_channel_events::GfxVersion;
using detail::graphics_channel_events::GraphicsChannelEvents;
using detail::graphics_channel_events::QoeAck;
}
