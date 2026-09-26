#pragma once
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/picture/forward.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <freerdp/freerdp.h>
#include <freerdp/server/rdpei.h>
#include <oxbox/utilities/utf-decode.hpp>
#include <array>
#include <cstdint>

namespace sdl_rdp::input::detail::events {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::diagnostics::FailureLog;
using sdl_rdp::link::Activation;
using sdl_rdp::link::EventQueue;
using sdl_rdp::link::PeerLink;
using sdl_rdp::link::SessionAccess;
using sdl_rdp::picture::DesktopLayout;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::utilities::OperationName;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Rect;

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
  template <class Result> auto WhenActive(Result idle, std::invocable auto action) -> Result;
  auto                         Motion(int x, int y)                                -> bool;
  auto                         Center()                                            -> bool;
  template <auto BUILD>
    requires std::invocable<decltype(BUILD), int, int, Rect>
  auto Scaled(int x, int y) -> void;
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

namespace sdl_rdp::input {
using detail::events::InputEvents;
using detail::events::MouseMode;
}
