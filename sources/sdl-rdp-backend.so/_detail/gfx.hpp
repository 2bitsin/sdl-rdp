#pragma once
#include "avc-encoder.hpp"
#include "avc-regions.hpp"
#include "avc.hpp"
#include "extent.hpp"
#include "gfx-protocol.hpp"
#include "graphics-timing.hpp"
#include "rdp-handles.hpp"
#include "sdl-rdp-backend.h"

#include <freerdp/codec/progressive.h>
#include <freerdp/server/rdpgfx.h>
#include <optional>
#include <span>
#include <vector>

namespace Backend {
inline constexpr UINT16 GraphicsSurfaceId = 1;
inline constexpr UINT32 GraphicsContextId = 1;
class Activation;
class Configuration;
class Diagnostics;
class Encoder;
class FramePacing;
class PeerFrames;
class PeerLink;
class Scaler;
class GfxChannel {
public:
       GfxChannel(GfxChannel const&)                             = delete;
       GfxChannel(GfxChannel&&)                                  = delete;
  GfxChannel(PeerLink& link, Diagnostics const& diagnostics, Configuration const& configuration, Activation& activation,
             PeerFrames& frames, FramePacing& pacing, Encoder& encoder, Scaler& scaler);
       ~GfxChannel();
  auto operator = (GfxChannel const&)             -> GfxChannel& = delete;
  auto operator = (GfxChannel&&)                  -> GfxChannel& = delete;
  auto Open()                                     -> bool;
  auto Pump()                                     -> bool;
  auto Event() const                              -> HANDLE;
  auto Prepare()                                  -> bool;
  auto Encode()                                   -> bool;
  auto Send()                                     -> bool;
  auto FrameWindow() const                        -> unsigned;
  auto Confirmed() const noexcept                 -> bool;
  auto Assigned(UINT32 channel_id) const noexcept -> bool;
  auto Timing() const noexcept                    -> GraphicsTiming const&;

private:
  struct Packet {
    sdlrdp_rect area  { };
    std::size_t offset{ };
    std::size_t length{ };
    UINT32      codec { };
  };
  auto CompressProgressive(REGION16& damage, std::chrono::steady_clock::time_point start) -> bool;
  auto        CodecChoice()                                                                  -> sdlrdp_codec;
  auto        AvcFailure()                                                                   -> std::string;
  auto        ProgressiveDamage(REGION16& damage)                                            -> bool;
  auto        FinishFrame()                                                                  -> bool;
  auto        LogCapabilities(std::span<RDPGFX_CAPSET const> advertised) const               -> void;
  auto        ActivateCapabilities(RDPGFX_CAPSET const& selected, bool wanted)               -> UINT;
  auto        ResetSurface()                                                                 -> bool;
  auto AvcTimes() const -> std::optional<Avc::EncodingTimes>;
  auto        Surface()                                                                      -> bool;
  auto        Select()                                                                       -> bool;
  auto        Progressive()                                                                  -> bool;
  auto        Avc420()                                                                       -> bool;
  auto        Picture()                                                                      -> std::span<BYTE const>;
  auto        SelectAvc()                                                                    -> bool;
  auto        ResetAvc()                                                                     -> void;
  auto        ConfirmedCapability(RDPGFX_CAPSET const& cap)                                  -> void;
  auto        ProgressivePayload(std::span<BYTE> data)                                       -> bool;
  auto        Raw()                                                                          -> bool;
  auto        Planar()                                                                       -> bool;
  auto        BeginPayload()                                                                 -> void;
  auto        WriteCommand(Packet const& packet)                                             -> bool;
  auto        Command(sdlrdp_rect area, std::span<BYTE const> data, UINT32 codec)            -> bool;
  auto        Check(UINT result, char const* operation) const                                -> bool;
  static auto Caps(RdpgfxServerContext* context, RDPGFX_CAPS_ADVERTISE_PDU const* caps)      -> UINT;
  static auto Ack(RdpgfxServerContext* context, RDPGFX_FRAME_ACKNOWLEDGE_PDU const* ack)     -> UINT;
  static auto Qoe(RdpgfxServerContext* context, RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const* ack) -> UINT;
  using GraphicsContext    = std::unique_ptr<RdpgfxServerContext, Releases<rdpgfx_server_context_free>>;
  using ProgressiveContext = std::unique_ptr<PROGRESSIVE_CONTEXT, Releases<progressive_context_free>>;
  PeerLink&             _link;
  Diagnostics const&    _diagnostics;
  Configuration const&  _configuration;
  Activation&           _activation;
  PeerFrames&           _frames;
  FramePacing&          _pacing;
  Encoder&              _encoder;
  Scaler&               _scaler;
  GraphicsContext       _context;
  ProgressiveContext    _progressive;
  Avc::Encoder          _avc;
  GraphicsTiming        _timing;
  std::optional<UINT32> _id;
  Extent                _surface      { };
  sdlrdp_codec          _requested    { SDLRDP_CODEC_AUTO };
  bool                  _confirmed    { };
  bool                  _avc_allowed  { };
  bool                  _avc_logged   { };
  bool                  _avc_rejected { };
  bool                  _force_idr    { true              };
  bool                  _headers      { };
  bool                  _logged       { };
  unsigned              _avc_rate     { };
  UINT32                _queue_depth  { };
  std::size_t           _frame_bytes  { };
  std::size_t           _last_bytes   { };
  Avc::Regions          _regions;
  std::vector<BYTE>     _payload;
  std::vector<Packet>   _prepared;
  std::vector<BYTE>     _pixels;
  std::vector<BYTE>     _band;
};
}
