#pragma once
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/diagnostics/logged-failures.hpp>
#include <sdl-rdp/freerdp-facade/display-channel.hpp>
#include <sdl-rdp/link/dynamic-channel.hpp>
#include <sdl-rdp/link/dynamic-channels.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/picture/forward.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace sdl_rdp::video::detail::display_control {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::diagnostics::LoggedFailures;
using sdl_rdp::freerdp_facade::DisplayChannel;
using sdl_rdp::freerdp_facade::DisplayChannelEvents;
using sdl_rdp::freerdp_facade::DisplayMonitor;
using sdl_rdp::link::Activation;
using sdl_rdp::link::DynamicChannel;
using sdl_rdp::link::DynamicChannels;
using sdl_rdp::link::EventQueue;
using sdl_rdp::link::PeerLink;
using sdl_rdp::picture::DesktopLayout;

class DisplayControl final : public DynamicChannel, public LoggedFailures<DisplayChannelEvents> {
public:
       DisplayControl(PeerLink& link, Activation const& activation, DesktopLayout const& desktop, EventQueue& events,
                      Diagnostics const& diagnostics) noexcept;
  auto Open()            -> bool;
  auto Opened() noexcept -> std::optional<std::reference_wrapper<DisplayChannelEvents>>;
  auto Activate()        -> bool override;
  auto Reject()          -> void override;

private:
  auto ChannelAssigned(std::uint32_t id)                       -> void override;
  auto MonitorLayout(std::span<DisplayMonitor const> monitors) -> bool override;
  PeerLink&                                  _link;
  Activation const&                          _activation;
  DesktopLayout const&                       _desktop;
  EventQueue&                                _events;
  DisplayChannel                             _channel;
  std::optional<DynamicChannels::Assignment> _assignment;
  bool                                       _open      { };
};
}

namespace sdl_rdp::video {
using detail::display_control::DisplayControl;
}
