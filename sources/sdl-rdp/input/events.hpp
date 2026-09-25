#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <freerdp/freerdp.h>
#include <freerdp/server/rdpei.h>
#include <oxbox/utilities/utf-decode.hpp>
#include <array>
#include <cstdint>

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
  auto Install(rdpInput& input)                                     -> void;
  auto Key(std::uint16_t flags, std::uint8_t code)                  -> bool;
  auto Text(std::uint16_t flags, std::uint16_t code)                -> bool;
  auto Mouse(std::uint16_t flags, std::uint16_t x, std::uint16_t y) -> bool;
  auto ExtendedMouse(std::uint16_t flags)                           -> bool;
  auto Pointer(std::uint64_t flags, std::int32_t x, std::int32_t y) -> std::uint32_t;
  auto Touch(RDPINPUT_TOUCH_EVENT const& event)                     -> std::uint32_t;
  auto Point(MouseMode mode) noexcept                               -> void;
  auto Failures(OperationName operation) const noexcept             -> FailureLog;

private:
  template <class Result> auto WhenActive(Result idle, std::invocable auto action)                    -> Result;
  auto                         Motion(int x, int y)                                                   -> bool;
  auto                         Center()                                                               -> bool;
  auto                         Scaled(int x, int y, std::invocable<int, int, sdlrdp_rect> auto build) -> void;
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
