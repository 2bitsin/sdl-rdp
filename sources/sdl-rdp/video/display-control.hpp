#pragma once
#include <sdl-rdp/core/channel-slot.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>

#include <freerdp/server/disp.h>

namespace Backend {
class Activation;
class DesktopLayout;
class EventQueue;
class PeerLink;
class DisplayControl final : public DynamicChannel {
public:
       DisplayControl(PeerLink& link, Activation const& activation, DesktopLayout const& desktop,
                      EventQueue& events) noexcept;
  auto Open()                                                -> bool;
  auto Opened() const noexcept                               -> DispServerContext*;
  auto Activate()                                            -> bool override;
  auto Reject()                                              -> void override;
  auto Layout(DISPLAY_CONTROL_MONITOR_LAYOUT_PDU const& pdu) -> UINT;

private:
  using DisplayContext = std::unique_ptr<DispServerContext, Releases<disp_server_context_free>>;
  PeerLink&            _link;
  Activation const&    _activation;
  DesktopLayout const& _desktop;
  EventQueue&          _events;
  DisplayContext       _context;
  ChannelSlot          _slot;
  bool                 _open      { };
};
}
