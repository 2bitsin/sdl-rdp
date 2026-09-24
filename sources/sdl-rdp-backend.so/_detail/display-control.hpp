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
  bool                Open();
  DispServerContext*  Opened() const noexcept;
  std::optional<BOOL> Activate(UINT32 channel_id);
  UINT                Layout(DISPLAY_CONTROL_MONITOR_LAYOUT_PDU const& pdu);

private:
  PeerLink&                                                              _link;
  Activation const&                                                      _activation;
  DesktopLayout const&                                                   _desktop;
  EventQueue&                                                            _events;
  std::unique_ptr<DispServerContext, Releases<disp_server_context_free>> _context;
  std::optional<UINT32>                                                  _id;
  bool                                                                   _open      { };
};
}
