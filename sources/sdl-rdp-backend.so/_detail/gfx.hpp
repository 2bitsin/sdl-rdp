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
                        GfxChannel(GfxChannel const&)     = delete;
                        GfxChannel(GfxChannel&&)          = delete;
  GfxChannel(PeerLink& link, Diagnostics const& diagnostics, Configuration const& configuration, Activation& activation,
             PeerFrames& frames, FramePacing& pacing, Encoder& encoder, Scaler& scaler);
                        ~GfxChannel();
  GfxChannel&           operator = (GfxChannel const&)    = delete;
  GfxChannel&           operator = (GfxChannel&&)         = delete;
  bool                  Open();
  bool                  Pump();
  HANDLE                Event() const;
  bool                  Prepare();
  bool                  Encode();
  bool                  Send();
  unsigned              FrameWindow() const;
  bool                  Confirmed() const                 noexcept;
  bool                  Assigned(UINT32 channel_id) const noexcept;
  GraphicsTiming const& Timing() const                    noexcept;

private:
  struct Packet {
    sdlrdp_rect area  { };
    std::size_t offset{ };
    std::size_t length{ };
    UINT32      codec { };
  };
  bool                              CompressProgressive(REGION16& damage, std::chrono::steady_clock::time_point start);
  sdlrdp_codec                      CodecChoice();
  std::string                       AvcFailure();
  bool                              ProgressiveDamage(REGION16& damage);
  bool                              FinishFrame();
  void                              LogCapabilities(std::span<RDPGFX_CAPSET const> advertised) const;
  UINT                              ActivateCapabilities(RDPGFX_CAPSET const& selected, bool wanted);
  bool                              ResetSurface();
  std::optional<Avc::EncodingTimes> AvcTimes() const;
  bool                              Surface();
  bool                              Select();
  bool                              Progressive();
  bool                              Avc420();
  std::span<BYTE const>             Picture();
  bool                              SelectAvc();
  void                              ResetAvc();
  void                              ConfirmedCapability(RDPGFX_CAPSET const& cap);
  bool                              ProgressivePayload(std::span<BYTE> data);
  bool                              Raw();
  bool                              Planar();
  void                              BeginPayload();
  bool                              WriteCommand(Packet const& packet);
  bool                              Command(sdlrdp_rect area, std::span<BYTE const> data, UINT32 codec);
  bool                              Check(UINT result, char const* operation) const;
  static UINT                       Caps(RdpgfxServerContext* context, RDPGFX_CAPS_ADVERTISE_PDU const* caps);
  static UINT                       Ack(RdpgfxServerContext* context, RDPGFX_FRAME_ACKNOWLEDGE_PDU const* ack);
  static UINT                       Qoe(RdpgfxServerContext* context, RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const* ack);
  PeerLink&                                                                  _link;
  Diagnostics const&                                                         _diagnostics;
  Configuration const&                                                       _configuration;
  Activation&                                                                _activation;
  PeerFrames&                                                                _frames;
  FramePacing&                                                               _pacing;
  Encoder&                                                                   _encoder;
  Scaler&                                                                    _scaler;
  std::unique_ptr<RdpgfxServerContext, Releases<rdpgfx_server_context_free>> _context;
  std::unique_ptr<PROGRESSIVE_CONTEXT, Releases<progressive_context_free>>   _progressive;
  Avc::Encoder                                                               _avc;
  GraphicsTiming                                                             _timing;
  std::optional<UINT32>                                                      _id;
  Extent                                                                     _surface      { };
  sdlrdp_codec                                                               _requested    { SDLRDP_CODEC_AUTO };
  bool                                                                       _confirmed    { };
  bool                                                                       _avc_allowed  { };
  bool                                                                       _avc_logged   { };
  bool                                                                       _avc_rejected { };
  bool                                                                       _force_idr    { true              };
  bool                                                                       _headers      { };
  bool                                                                       _logged       { };
  unsigned                                                                   _avc_rate     { };
  UINT32                                                                     _queue_depth  { };
  std::size_t                                                                _frame_bytes  { };
  std::size_t                                                                _last_bytes   { };
  Avc::Regions                                                               _regions;
  std::vector<BYTE>                                                          _payload;
  std::vector<Packet>                                                        _prepared;
  std::vector<BYTE>                                                          _pixels;
  std::vector<BYTE>                                                          _band;
};
}
