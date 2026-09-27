#pragma once
#include <sdl-rdp/configuration/forward.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/diagnostics/logged-failures.hpp>
#include <sdl-rdp/freerdp-facade/graphics-channel-events.hpp>
#include <sdl-rdp/freerdp-facade/graphics-channel.hpp>
#include <sdl-rdp/freerdp-facade/progressive-encoder.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/link/dynamic-channels.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/video/avc/encoder.hpp>
#include <sdl-rdp/video/avc/encoding.hpp>
#include <sdl-rdp/video/avc/regions.hpp>
#include <sdl-rdp/video/forward.hpp>
#include <sdl-rdp/video/frame/forward.hpp>
#include <sdl-rdp/video/gfx/protocol.hpp>

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
using sdl_rdp::diagnostics::LoggedFailures;
using sdl_rdp::freerdp_facade::FrameAck;
using sdl_rdp::freerdp_facade::GfxCapability;
using sdl_rdp::freerdp_facade::GfxCodec;
using sdl_rdp::freerdp_facade::GraphicsChannel;
using sdl_rdp::freerdp_facade::GraphicsChannelEvents;
using sdl_rdp::freerdp_facade::ProgressiveEncoder;
using sdl_rdp::freerdp_facade::QoeAck;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::link::Activation;
using sdl_rdp::link::DynamicChannel;
using sdl_rdp::link::DynamicChannels;
using sdl_rdp::link::PeerLink;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Rect;
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
  std::chrono::nanoseconds ready_time{ };
  QoeAck                   qoe       { };
};
class GfxChannel final : public LoggedFailures<GraphicsChannelEvents> {
public:
  GfxChannel(PeerLink& link, Diagnostics const& diagnostics, Configuration const& configuration, Activation& activation,
             FrameSources sources, DynamicChannel& owner);
       ~GfxChannel() override;
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
  struct Packet {
    Rect        area  { };
    std::size_t offset{ };
    std::size_t length{ };
    GfxCodec    codec { };
  };
  auto CapsAdvertise(std::span<GfxCapability const> advertised)            -> bool override;
  auto FrameAcknowledge(FrameAck ack)                                      -> void override;
  auto QoeFrameAcknowledge(QoeAck ack)                                     -> void override;
  auto ChannelAssigned(std::uint32_t id)                                   -> void override;
  auto CodecChoice()                                                       -> Codec;
  auto AvcFailure()                                                        -> std::string;
  auto FinishFrame()                                                       -> bool;
  auto LogCapabilities(std::span<GfxCapability const> advertised) const    -> void;
  auto ActivateCapabilities(GfxCapability selected, bool wanted)           -> void;
  auto ResetSurface()                                                      -> bool;
  auto AvcTimes() const                                                    -> std::optional<EncodingTimes>;
  auto Surface()                                                           -> bool;
  auto Select()                                                            -> bool;
  auto Progressive()                                                       -> bool;
  auto Avc420()                                                            -> bool;
  auto Picture()                                                           -> std::span<std::uint8_t const>;
  auto SelectAvc()                                                         -> bool;
  auto ResetAvc()                                                          -> void;
  auto ConfirmedCapability(GfxCapability cap)                              -> void;
  auto ProgressivePayload(std::span<std::byte const> data)                 -> bool;
  auto Raw()                                                               -> bool;
  auto Planar()                                                            -> bool;
  auto BeginPayload()                                                      -> void;
  auto WriteCommand(Packet const& packet)                                  -> bool;
  auto Command(Rect area, std::span<std::byte const> data, GfxCodec codec) -> bool;
  auto Sent(bool sent, std::string_view operation) const                   -> bool;
  PeerLink&                                  _link;
  Configuration const&                       _configuration;
  Activation&                                _activation;
  FrameSources                               _sources;
  GraphicsChannel                            _channel;
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
  FrameAck                                   _acknowledged { };
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
