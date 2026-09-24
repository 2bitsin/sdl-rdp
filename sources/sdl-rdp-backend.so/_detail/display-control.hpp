#pragma once
#include "pinned.hpp"
#include "rdp-handles.hpp"

#include <freerdp/server/disp.h>
#include <optional>

namespace Backend {
class Activation;
class DesktopLayout;
class EventQueue;
class PeerLink;
class DisplayControl : private Pinned {
public:
       DisplayControl(PeerLink& link, Activation const& activation, DesktopLayout const& desktop,
                      EventQueue& events) noexcept;
  auto Open()                                                -> bool;
  auto Opened() const noexcept                               -> DispServerContext*;
  auto Activate(UINT32 channel_id)                           -> std::optional<BOOL>;
  auto Layout(DISPLAY_CONTROL_MONITOR_LAYOUT_PDU const& pdu) -> UINT;

private:
  using DisplayContext = std::unique_ptr<DispServerContext, Releases<disp_server_context_free>>;
  PeerLink&             _link;
  Activation const&     _activation;
  DesktopLayout const&  _desktop;
  EventQueue&           _events;
  DisplayContext        _context;
  std::optional<UINT32> _id;
  bool                  _open      { };
};
}
