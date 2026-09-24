#pragma once
#include "pinned.hpp"
#include "sdl-rdp-backend.h"

#include <array>
#include <freerdp/freerdp.h>
#include <freerdp/server/rdpei.h>
#include <oxbox/utilities/utf-decode.hpp>

namespace Backend {
class Activation;
class DesktopLayout;
class Diagnostics;
class EventQueue;
class FrameStore;
class PeerLink;
class SessionAccess;
enum class MouseMode{ Absolute, Relative };
struct MouseState {
  MouseMode mode          { MouseMode::Absolute };
  bool      have_relative { };
  bool      warp_requested{ };
  int       last_x        { };
  int       last_y        { };
};
class InputEvents : private Pinned {
public:
       InputEvents(PeerLink& link, Activation const& activation, DesktopLayout const& desktop, EventQueue& events,
                   FrameStore& store, Diagnostics const& diagnostics, SessionAccess& session) noexcept;
  void Install(rdpInput& input);
  BOOL Key(UINT16 flags, UINT8 code);
  BOOL Text(UINT16 flags, UINT16 code);
  BOOL Mouse(UINT16 flags, UINT16 x, UINT16 y);
  BOOL ExtendedMouse(UINT16 flags);
  UINT Pointer(UINT64 flags, INT32 x, INT32 y);
  UINT Touch(RDPINPUT_TOUCH_EVENT const& event);
  void Point(MouseMode mode) noexcept;

private:
  template <class Result> Result WhenActive(Result idle, std::invocable auto action);
  bool                           Motion(int x, int y);
  bool                           Center();
  void                           Scaled(int x, int y, std::invocable<int, int, sdlrdp_rect> auto build);
  PeerLink&                                       _link;
  Activation const&                               _activation;
  DesktopLayout const&                            _desktop;
  EventQueue&                                     _events;
  FrameStore&                                     _store;
  Diagnostics const&                              _diagnostics;
  SessionAccess&                                  _session;
  std::array<oxbox::utilities::UtfDecodeState, 2> _unicode    { };
  MouseState                                      _mouse      { };
};
}
