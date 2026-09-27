#pragma once
#include <sdl-rdp/freerdp-facade/channel-manager.hpp>
#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/freerdp-facade/display-channel-events.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <cstdint>
#include <memory>

struct s_disp_server_context;

namespace sdl_rdp::freerdp_facade::detail::display_channel {
using sdl_rdp::freerdp_facade::ChannelManager;
using sdl_rdp::freerdp_facade::Connection;
using sdl_rdp::freerdp_facade::DisplayChannelEvents;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Releases;

// abi: the release step of a display control server context, handed the context.
auto ReleaseDisplay(s_disp_server_context* context) noexcept -> void;
using DisplayContext = std::unique_ptr<s_disp_server_context, Releases<ReleaseDisplay>>;
// The server's limits on a client layout (MS-RDPEDISP 2.2.2.1); FreeRDP also refuses a layout over max_monitors.
struct DisplayCaps {
  std::uint32_t max_monitors     { };
  std::uint32_t max_area_factor_a{ };
  std::uint32_t max_area_factor_b{ };
};
// The display control dynamic channel of a connection.
class DisplayChannel : private Pinned {
public:
       DisplayChannel(ChannelManager& channels, Connection& connection, DisplayChannelEvents& events) noexcept;
  auto Open(DisplayCaps caps) -> bool;
  auto Close() noexcept       -> void;
  auto Caps()                 -> bool;

private:
  class Slots;
  // The unit test drives the slots through the context.
  friend class DisplayChannelProbe;
  auto Context() const -> s_disp_server_context&;
  ChannelManager&       _channels;
  Connection&           _connection;
  DisplayChannelEvents& _events;
  DisplayContext        _context;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::display_channel::DisplayCaps;
using detail::display_channel::DisplayChannel;
}
