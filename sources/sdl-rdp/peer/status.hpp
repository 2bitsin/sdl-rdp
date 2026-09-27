#pragma once
#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/video/gfx/channel.hpp>

#include <freerdp/server/disp.h>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>

namespace sdl_rdp::peer::detail::status {
using sdl_rdp::freerdp_facade::Connection;
using sdl_rdp::utilities::Rect;
using sdl_rdp::video::gfx::GraphicsTiming;

struct PeerStatus {
  std::reference_wrapper<Connection>                       connection;
  std::optional<std::reference_wrapper<DispServerContext>> display;
  Rect                                                     desktop         { };
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
