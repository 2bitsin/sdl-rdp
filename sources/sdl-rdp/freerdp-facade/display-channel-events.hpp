#pragma once
#include <sdl-rdp/freerdp-facade/failure-sink.hpp>

#include <cstdint>
#include <span>

namespace sdl_rdp::freerdp_facade::detail::display_channel_events {
// One monitor of a client's layout, in virtual desktop pixels (MS-RDPEDISP 2.2.2.2.1).
struct DisplayMonitor {
  std::int32_t  left  { };
  std::int32_t  top   { };
  std::uint32_t width { };
  std::uint32_t height{ };
};
// What a display control channel reports: its dynamic channel id, then each monitor layout the client requests.
class DisplayChannelEvents : public FailureSink {
public:
  virtual auto ChannelAssigned(std::uint32_t id) -> void = 0;
  // False when the layout is invalid data.
  virtual auto MonitorLayout(std::span<DisplayMonitor const> monitors) -> bool = 0;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::display_channel_events::DisplayChannelEvents;
using detail::display_channel_events::DisplayMonitor;
}
