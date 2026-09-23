#pragma once
#include "rdp-handles.hpp"
#include "sdl-rdp-backend.h"
#include <freerdp/server/rdpgfx.h>
#include <freerdp/codec/progressive.h>
#include <span>
#include "gfx-protocol.hpp"
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
  bool Send();
  bool Budget() const;
  bool confirmed = false;
private:
  bool Surface();
  bool Select();
  bool Progressive();
  bool ProgressivePayload(std::span<BYTE> data);
  bool Raw();
  bool Planar();
  bool WriteCommand(sdlrdp_rect area, std::span<BYTE> data, UINT32 codec);
  bool Prepare();
  bool Command(sdlrdp_rect area, std::span<BYTE> data, UINT32 codec);
  bool Check(UINT result, char const* operation);
  static UINT Caps(RdpgfxServerContext*, RDPGFX_CAPS_ADVERTISE_PDU const*);
  static UINT Ack(RdpgfxServerContext*, RDPGFX_FRAME_ACKNOWLEDGE_PDU const*);
  static UINT Qoe(RdpgfxServerContext*, RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU const*);
  Peer& peer;
  std::unique_ptr<RdpgfxServerContext, Releases<rdpgfx_server_context_free>> context;
  std::unique_ptr<PROGRESSIVE_CONTEXT, Releases<progressive_context_free>> progressive;
  bool headers = false, logged = false;
  unsigned width = 0, height = 0;
  UINT32 queue_depth = 0;
  std::size_t frame_bytes = 0, last_bytes = 0;
  struct Packet { sdlrdp_rect area; std::vector<BYTE> data; UINT32 codec; };
  std::vector<Packet> prepared;
  std::vector<BYTE> pixels, band;
};
}
