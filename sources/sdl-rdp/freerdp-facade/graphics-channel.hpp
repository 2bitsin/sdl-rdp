#pragma once
#include <sdl-rdp/freerdp-facade/channel-manager.hpp>
#include <sdl-rdp/freerdp-facade/graphics-channel-events.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

struct s_rdpgfx_server_context;

namespace sdl_rdp::freerdp_facade::detail::graphics_channel {
using sdl_rdp::freerdp_facade::ChannelManager;
using sdl_rdp::freerdp_facade::GfxCapability;
using sdl_rdp::freerdp_facade::GraphicsChannelEvents;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::Releases;

// abi: the release step of a graphics server context, handed the context.
auto ReleaseGraphics(s_rdpgfx_server_context* context) noexcept -> void;
using GraphicsContext = std::unique_ptr<s_rdpgfx_server_context, Releases<ReleaseGraphics>>;
enum class GfxCodec : std::uint8_t { Uncompressed, Planar, Progressive, Avc420 };
struct SurfaceSpec {
  std::uint16_t id  { };
  Extent        size{ };
};
struct GraphicsMonitor {
  Rect area   { };
  bool primary{ };
};
// MS-RDPEGFX 2.2.4.4.1 carries a quality per region; the project encodes every region at one.
struct QuantQuality {
  std::uint8_t qp         { };
  bool         progressive{ };
  std::uint8_t quality    { };
};
struct Avc420Metablock {
  std::span<Rect const> regions;
  QuantQuality          quality{ };
};
// An AVC420 command carries its metablock; no other codec does.
struct GraphicsCommand {
  std::uint16_t              surface  { };
  std::uint32_t              context  { };
  GfxCodec                   codec    { };
  Rect                       area     { };
  std::span<std::byte const> payload;
  Avc420Metablock            metablock{ };
};
// The rdpgfx dynamic channel of a connection, polled by the server: no channel thread.
class GraphicsChannel : private Pinned {
public:
       GraphicsChannel(ChannelManager& channels, GraphicsChannelEvents& events) noexcept;
       ~GraphicsChannel();
  auto Open()                                                                    -> bool;
  auto Pump()                                                                    -> bool;
  auto Handle() const                                                            -> WaitHandle;
  auto CapsConfirm(GfxCapability capability)                                     -> bool;
  auto ResetGraphics(Extent desktop, std::span<GraphicsMonitor const> monitors)  -> bool;
  auto CreateSurface(SurfaceSpec surface)                                        -> bool;
  auto DeleteSurface(std::uint16_t surface)                                      -> bool;
  auto MapSurfaceToOutput(std::uint16_t surface)                                 -> bool;
  auto DeleteEncodingContext(std::uint16_t surface, std::uint32_t context)       -> bool;
  auto StartFrame(std::uint32_t frame, std::chrono::system_clock::time_point at) -> bool;
  auto EndFrame(std::uint32_t frame)                                             -> bool;
  auto SurfaceCommand(GraphicsCommand const& command)                            -> bool;

private:
  // The AVC420 metablock's wire arrays, kept across frames so a frame allocates nothing.
  class Avc420Buffers;
  // The unit test drives the slots through the context.
  friend class GraphicsChannelProbe;
  auto Context() const -> s_rdpgfx_server_context&;
  ChannelManager&                _channels;
  GraphicsChannelEvents&         _events;
  GraphicsContext                _context;
  std::unique_ptr<Avc420Buffers> _avc420;
};
// MS-RDPEGFX 2.2.2.11: the UTC time of day a frame starts, packed as hours, minutes, seconds and milliseconds.
auto FrameTimestamp(std::chrono::system_clock::time_point at) -> std::uint32_t;
}

namespace sdl_rdp::freerdp_facade {
using detail::graphics_channel::Avc420Metablock;
using detail::graphics_channel::FrameTimestamp;
using detail::graphics_channel::GfxCodec;
using detail::graphics_channel::GraphicsChannel;
using detail::graphics_channel::GraphicsCommand;
using detail::graphics_channel::GraphicsMonitor;
using detail::graphics_channel::QuantQuality;
using detail::graphics_channel::SurfaceSpec;
}
