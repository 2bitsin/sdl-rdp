#pragma once
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/link/dynamic-channel.hpp>
#include <sdl-rdp/link/dynamic-channels.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/picture/forward.hpp>

#include <freerdp/server/disp.h>
#include <cstdint>
#include <functional>
#include <optional>

namespace sdl_rdp::video::detail::display_control {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::link::Activation;
using sdl_rdp::link::DynamicChannel;
using sdl_rdp::link::DynamicChannels;
using sdl_rdp::link::EventQueue;
using sdl_rdp::link::PeerLink;
using sdl_rdp::picture::DesktopLayout;
using sdl_rdp::utilities::Releases;

class DisplayControl final : public DynamicChannel {
public:
       DisplayControl(PeerLink& link, Activation const& activation, DesktopLayout const& desktop, EventQueue& events,
                      Diagnostics const& diagnostics) noexcept;
  auto Open()                  -> bool;
  auto Opened() const noexcept -> std::optional<std::reference_wrapper<DispServerContext>>;
  auto Activate()              -> bool override;
  auto Reject()                -> void override;

private:
  class Callbacks;
  auto Layout(DISPLAY_CONTROL_MONITOR_LAYOUT_PDU const& pdu) -> std::uint32_t;
  auto Assign(std::uint32_t id)                              -> bool;
  auto FailureSource() const noexcept                        -> Diagnostics const&;
  using DisplayContext = std::unique_ptr<DispServerContext, Releases<disp_server_context_free>>;
  PeerLink&                                  _link;
  Activation const&                          _activation;
  DesktopLayout const&                       _desktop;
  EventQueue&                                _events;
  Diagnostics const&                         _diagnostics;
  DisplayContext                             _context;
  std::optional<DynamicChannels::Assignment> _assignment;
  bool                                       _open       { };
};
}

namespace sdl_rdp::video {
using detail::display_control::DisplayControl;
}
