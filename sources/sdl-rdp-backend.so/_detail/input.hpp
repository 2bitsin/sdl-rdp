#pragma once
#include "rdp-handles.hpp"

#include <array>
#include <freerdp/freerdp.h>
#include <freerdp/peer.h>
#include <freerdp/server/ainput.h>
#include <freerdp/server/rdpei.h>
#include <oxbox/utilities/utf-decode.hpp>
#include <span>

namespace Backend {
class Peer;
struct Input {
public:
  static Input& Held(Peer& /*peer*/);
  bool Channels(Peer& peer, std::span<HANDLE const> ready);
  bool Open(Peer& peer);
  unsigned Handles(HANDLE* handles) const;
  void Close();
  static void Relative(Peer& /*peer*/, int dx, int dy);
  static bool Motion(Peer& /*peer*/, int x, int y);
  static bool Center(Peer& /*peer*/);
  void RelativeMode(bool enabled) {
    relative       = enabled;
    warp_requested = false;
  }

private:
  static BOOL Create(freerdp_peer* /*unused*/, rdpContext* /*context*/);
  static void Free(freerdp_peer* /*unused*/, rdpContext* /*context*/);
  static BOOL Unicode(rdpInput* /*input*/, UINT16 flags, UINT16 code);
  static UINT Advanced(ainput_server_context* /*context*/, UINT64 /*unused*/, UINT64 flags, INT32 x, INT32 y);
  static UINT Touch(RdpeiServerContext* /*context*/, RDPINPUT_TOUCH_EVENT const* /*event*/);
  void InstallChannels(Peer& peer);
  friend class Peer;
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
};
struct InputContext : rdpContext {
  Input* state;
};
} // namespace Backend
