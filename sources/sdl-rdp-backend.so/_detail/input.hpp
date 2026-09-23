#pragma once
#include <span>
#include "rdp-handles.hpp"
#include <freerdp/peer.h>
#include <freerdp/server/ainput.h>
#include <freerdp/server/rdpei.h>
#include <oxbox/utilities/utf-decode.hpp>
#include <array>

namespace Backend {
class Peer;
struct Input {
  static constexpr unsigned MaxHandles = 2;
  std::array<oxbox::utilities::UtfDecodeState, 2> unicode{};
  std::unique_ptr<ainput_server_context, Releases<ainput_server_context_free>> advanced;
  std::unique_ptr<RdpeiServerContext, Releases<rdpei_server_context_free>> touch;
  HANDLE advanced_event{ nullptr };
  bool opened = false, have_relative = false, relative = false;
  UINT32 advanced_id = UINT32_MAX, touch_id = UINT32_MAX;
  bool advanced_ready = false, touch_ready = false;
  bool warp_requested = false;
  int last_x = 0, last_y = 0;
  static Input& Held(Peer&);
  static BOOL Create(freerdp_peer*, rdpContext*);
  static void Free(freerdp_peer*, rdpContext*);
  bool Channels(Peer& peer, std::span<HANDLE const> ready);
  bool Open(Peer& peer);
  unsigned Handles(HANDLE* handles);
  void Close();
  static BOOL Unicode(rdpInput*, UINT16 flags, UINT16 code);
  static UINT Advanced(ainput_server_context*, UINT64, UINT64 flags, INT32 x, INT32 y);
  static UINT Touch(RdpeiServerContext*, const RDPINPUT_TOUCH_EVENT*);
  static void Relative(Peer&, int dx, int dy);
  static bool Motion(Peer&, int x, int y);
  static bool Center(Peer&);
};
struct InputContext : rdpContext { Input* state; };
}
