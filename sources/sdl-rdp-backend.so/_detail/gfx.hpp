#pragma once
#include "avc.hpp"
#include "gfx-protocol.hpp"
#include "rdp-handles.hpp"
#include "sdl-rdp-backend.h"

#include <freerdp/codec/progressive.h>
#include <freerdp/server/rdpgfx.h>
#include <span>
#include <vector>

namespace Backend {
inline constexpr UINT16 GraphicsSurfaceId = 1;
inline constexpr UINT32 GraphicsContextId = 1;
class                   Peer;
class GfxChannel {
public:
              GfxChannel(GfxChannel const&)  = delete;
              GfxChannel(GfxChannel&&)       = delete;
  explicit    GfxChannel(Peer& value);
              ~GfxChannel();
  GfxChannel& operator = (GfxChannel const&) = delete;
  GfxChannel& operator = (GfxChannel&&)      = delete;
  bool        Open();
  bool        Pump();
  HANDLE      Event() const;
  bool        Prepare();
  bool        Encode();
  bool        Send();
  unsigned    FrameWindow() const;
  bool        Confirmed() const              { return confirmed; }

private:
  bool                  CompressProgressive(REGION16& damage, std::chrono::steady_clock::time_point start);
  sdlrdp_codec          CodecChoice();
  std::string           AvcFailure();
  bool                  ProgressiveDamage(REGION16& damage);
  bool                  FinishFrame();
  void                  LogCapabilities(std::span<RDPGFX_CAPSET const> advertised) const;
  UINT                  ActivateCapabilities(RDPGFX_CAPSET const& selected, bool wanted);
  bool                  ResetSurface();
  void                  AccountAvcFrame();
  bool                  Surface();
  bool                  Select();
  bool                  Progressive();
  bool                  Avc420();
  std::span<BYTE const> Picture();
  bool                  SelectAvc();
  void                  ResetAvc();
  void                  ConfirmedCapability(RDPGFX_CAPSET const& cap);
  bool                  ProgressivePayload(std::span<BYTE> data);
  bool                  Raw();
  bool                  Planar();
  bool                  WriteCommand(sdlrdp_rect area, std::span<BYTE> data, UINT32 codec, Avc::Regions& regions);
  bool                  Command(sdlrdp_rect area, std::span<BYTE const> data, UINT32 codec);
  bool                  Check(UINT result, char const* operation) const;
  static UINT           Caps(RdpgfxServerContext* /*context*/, RDPGFX_CAPS_ADVERTISE_PDU const* /*caps*/);
  static UINT           Ack(RdpgfxServerContext* /*context*/, RDPGFX_FRAME_ACKNOWLEDGE_PDU const* /*ack*/);
  static UINT           Qoe(RdpgfxServerContext* /*context*/, RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const* /*ack*/);
  bool                                                                       confirmed    = false;
  Peer&                                                                      peer;
  std::unique_ptr<RdpgfxServerContext, Releases<rdpgfx_server_context_free>> context;
  std::unique_ptr<PROGRESSIVE_CONTEXT, Releases<progressive_context_free>>   progressive;
  Avc::Encoder                                                               avc;
  sdlrdp_codec                                                               requested    = SDLRDP_CODEC_AUTO;
  bool                                                                       avc_allowed  = false;
  bool                                                                       avc_logged   = false;
  bool                                                                       avc_rejected = false;
  bool                                                                       force_idr    = true;
  bool                                                                       headers      = false;
  bool                                                                       logged       = false;
  unsigned                                                                   width        = 0;
  unsigned                                                                   height       = 0;
  unsigned                                                                   avc_rate     = 0;
  UINT32                                                                     queue_depth  = 0;
  std::size_t                                                                frame_bytes  = 0;
  std::size_t                                                                last_bytes   = 0;
  struct Packet {
    sdlrdp_rect area  { };
    std::size_t offset{ };
    std::size_t length{ };
    UINT32      codec { };
  };
  Avc::Regions        regions;
  std::vector<BYTE>   payload;
  std::vector<Packet> prepared;
  std::vector<BYTE>   pixels;
  std::vector<BYTE>   band;
};
} // namespace Backend
