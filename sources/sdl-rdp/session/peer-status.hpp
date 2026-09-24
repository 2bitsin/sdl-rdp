#pragma once
#include <sdl-rdp/video/graphics-timing.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <freerdp/peer.h>
#include <freerdp/server/disp.h>
#include <chrono>
#include <cstdint>
#include <optional>

namespace Backend {
struct PeerStatus {
  freerdp_peer*                         client          { };
  DispServerContext*                    display         { };
  sdlrdp_rect                           desktop         { };
  bool                                  resizing        { };
  bool                                  holding         { };
  std::chrono::steady_clock::time_point activated_at;
  std::optional<GraphicsTiming>         graphics;
  std::uint32_t                         frame           { };
  std::uint64_t                         acknowledged    { };
  std::uint64_t                         acknowledgements{ };
  std::chrono::nanoseconds              encode_time     { };
};
}
