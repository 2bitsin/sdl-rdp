#pragma once
#include "rdp-handles.hpp"
#include "sdl-rdp-backend.h"
#include <freerdp/server/rdpgfx.h>
#include <freerdp/codec/progressive.h>
#include <span>
#include "gfx-protocol.hpp"
#include "avc.hpp"
#include <vector>

namespace Backend {
inline constexpr UINT16 GraphicsSurfaceId = 1;
inline constexpr UINT32 GraphicsContextId = 1;
class Peer;
class GfxChannel {
public:
  explicit GfxChannel(Peer& peer);
  ~GfxChannel();
  bool Open();
  bool Pump();
  HANDLE Event() const;
  bool Prepare();
  bool Encode();
  bool Send();
  unsigned FrameWindow() const;
  bool confirmed = false;
private:
  void AccountAvcFrame();
  bool Surface();
  bool Select();
  bool Progressive();
  bool Avc420();
  bool SelectAvc();
  void ResetAvc();
  void ConfirmedCapability(RDPGFX_CAPSET const& cap);
  bool ProgressivePayload(std::span<BYTE> data);
  bool Raw();
  bool Planar();
  bool WriteCommand(sdlrdp_rect area, std::span<BYTE> data, UINT32 codec, Avc::Regions& regions);
  bool Command(sdlrdp_rect area, std::span<BYTE const> data, UINT32 codec, Avc::Regions regions = {});
  bool Check(UINT result, char const* operation);
  static UINT Caps(RdpgfxServerContext*, RDPGFX_CAPS_ADVERTISE_PDU const*);
  static UINT Ack(RdpgfxServerContext*, RDPGFX_FRAME_ACKNOWLEDGE_PDU const*);
  static UINT Qoe(RdpgfxServerContext*, RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const*);
  Peer& peer;
  std::unique_ptr<RdpgfxServerContext, Releases<rdpgfx_server_context_free>> context;
  std::unique_ptr<PROGRESSIVE_CONTEXT, Releases<progressive_context_free>> progressive;
  Avc::Encoder avc;
  sdlrdp_codec requested = SDLRDP_CODEC_AUTO;
  bool avc_allowed = false, avc_logged = false, avc_rejected = false, force_idr = true;
  bool headers = false, logged = false;
  unsigned width = 0, height = 0;
  UINT32 queue_depth = 0;
  std::size_t frame_bytes = 0, last_bytes = 0;
  struct Packet { sdlrdp_rect area; std::vector<BYTE> data; UINT32 codec; Avc::Regions regions; };
  std::vector<Packet> prepared;
  std::vector<BYTE> pixels, band;
};
}
