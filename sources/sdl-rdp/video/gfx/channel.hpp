#pragma once
#include <sdl-rdp/configuration/forward.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/freerdp-facade/progressive-encoder.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/link/dynamic-channels.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/avc/encoder.hpp>
#include <sdl-rdp/video/avc/encoding.hpp>
#include <sdl-rdp/video/avc/regions.hpp>
#include <sdl-rdp/video/forward.hpp>
#include <sdl-rdp/video/frame/forward.hpp>
#include <sdl-rdp/video/gfx/protocol.hpp>

#include <freerdp/server/rdpgfx.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace sdl_rdp::video::gfx::detail::channel {
using sdl_rdp::configuration::Codec;
using sdl_rdp::configuration::Configuration;
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::freerdp_facade::ProgressiveEncoder;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::link::Activation;
using sdl_rdp::link::DynamicChannel;
using sdl_rdp::link::DynamicChannels;
using sdl_rdp::link::PeerLink;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::Releases;
using sdl_rdp::video::avc::Encoder;
using sdl_rdp::video::avc::EncodingTimes;
using sdl_rdp::video::avc::Regions;

inline constexpr std::uint16_t GraphicsSurfaceId = 1;
inline constexpr std::uint32_t GraphicsContextId = 1;
struct FrameSources {
  std::reference_wrapper<PeerFrames>                         frames;
  std::reference_wrapper<sdl_rdp::video::frame::FramePacing> pacing;
  std::reference_wrapper<sdl_rdp::video::Encoder>            encoder;
  std::reference_wrapper<Scaler>                             scaler;
};
struct GraphicsTiming {
  std::chrono::nanoseconds         ready_time{ };
  RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU qoe       { };
};
class GfxChannel : private Pinned {
public:
  GfxChannel(PeerLink& link, Diagnostics const& diagnostics, Configuration const& configuration, Activation& activation,
             FrameSources sources, DynamicChannel& owner);
       ~GfxChannel();
  auto Open()                     -> bool;
  auto Pump()                     -> bool;
  auto Event() const              -> WaitHandle;
  auto Prepare()                  -> bool;
  auto Encode()                   -> bool;
  auto Send()                     -> bool;
  auto FrameWindow() const        -> std::size_t;
  auto Confirmed() const noexcept -> bool;
  auto Timing() const noexcept    -> GraphicsTiming const&;

private:
  class Callbacks;
  struct Packet {
    Rect          area  { };
    std::size_t   offset{ };
    std::size_t   length{ };
    std::uint32_t codec { };
  };
  auto Caps(RDPGFX_CAPS_ADVERTISE_PDU const& caps)                              -> std::uint32_t;
  auto Assign(std::uint32_t id)                                                 -> bool;
  auto FailureSource() const noexcept                                           -> Diagnostics const&;
  auto Ack(RDPGFX_FRAME_ACKNOWLEDGE_PDU const& ack)                             -> std::uint32_t;
  auto Qoe(RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const& ack)                         -> std::uint32_t;
  auto CodecChoice()                                                            -> Codec;
  auto AvcFailure()                                                             -> std::string;
  auto FinishFrame()                                                            -> bool;
  auto LogCapabilities(std::span<RDPGFX_CAPSET const> advertised) const         -> void;
  auto ActivateCapabilities(RDPGFX_CAPSET const& selected, bool wanted)         -> std::uint32_t;
  auto ResetSurface()                                                           -> bool;
  auto AvcTimes() const                                                         -> std::optional<EncodingTimes>;
  auto Surface()                                                                -> bool;
  auto Select()                                                                 -> bool;
  auto Progressive()                                                            -> bool;
  auto Avc420()                                                                 -> bool;
  auto Picture()                                                                -> std::span<std::uint8_t const>;
  auto SelectAvc()                                                              -> bool;
  auto ResetAvc()                                                               -> void;
  auto ConfirmedCapability(RDPGFX_CAPSET const& cap)                            -> void;
  auto ProgressivePayload(std::span<std::byte const> data)                      -> bool;
  auto Raw()                                                                    -> bool;
  auto Planar()                                                                 -> bool;
  auto BeginPayload()                                                           -> void;
  auto WriteCommand(Packet const& packet)                                       -> bool;
  auto Command(Rect area, std::span<std::byte const> data, std::uint32_t codec) -> bool;
  auto Check(std::uint32_t result, std::string_view operation) const            -> bool;
  using GraphicsContext = std::unique_ptr<RdpgfxServerContext, Releases<rdpgfx_server_context_free>>;
  PeerLink&                                  _link;
  Diagnostics const&                         _diagnostics;
  Configuration const&                       _configuration;
  Activation&                                _activation;
  FrameSources                               _sources;
  GraphicsContext                            _context;
  std::optional<ProgressiveEncoder>          _progressive;
  Encoder                                    _avc;
  GraphicsTiming                             _timing;
  DynamicChannel&                            _owner;
  std::optional<DynamicChannels::Assignment> _assignment;
  Extent                                     _surface      { };
  Codec                                      _requested    { Codec::Auto };
  bool                                       _confirmed    { };
  bool                                       _avc_allowed  { };
  bool                                       _avc_logged   { };
  bool                                       _avc_rejected { };
  bool                                       _force_idr    { true        };
  bool                                       _headers      { };
  bool                                       _logged       { };
  std::uint32_t                              _avc_rate     { };
  std::uint32_t                              _queue_depth  { };
  std::size_t                                _frame_bytes  { };
  std::size_t                                _last_bytes   { };
  Regions                                    _regions;
  std::vector<std::byte>                     _payload;
  std::vector<Packet>                        _prepared;
  std::vector<std::uint8_t>                  _pixels;
  std::vector<std::uint8_t>                  _band;
};
}

namespace sdl_rdp::video::gfx {
using detail::channel::FrameSources;
using detail::channel::GfxChannel;
using detail::channel::GraphicsTiming;
}
