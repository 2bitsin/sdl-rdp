#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/video/graphics-timing.hpp>

#include <freerdp/peer.h>
#include <freerdp/server/disp.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

namespace sdl_rdp::peer::detail::status {
using sdl_rdp::video::GraphicsTiming;

struct PeerStatus {
  std::reference_wrapper<freerdp_peer>                     client;
  std::optional<std::reference_wrapper<DispServerContext>> display;
  sdlrdp_rect                                              desktop         { };
  bool                                                     resizing        { };
  bool                                                     holding         { };
  std::chrono::steady_clock::time_point                    activated_at;
  std::optional<GraphicsTiming>                            graphics;
  std::uint32_t                                            frame           { };
  std::uint64_t                                            acknowledged    { };
  std::uint64_t                                            acknowledgements{ };
  std::chrono::nanoseconds                                 encode_time     { };
};
}

namespace sdl_rdp::peer {
using detail::status::PeerStatus;
}
