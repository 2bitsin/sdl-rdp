#pragma once
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/diagnostics/logged-failures.hpp>
#include <sdl-rdp/freerdp-facade/advanced-input-channel-events.hpp>
#include <sdl-rdp/freerdp-facade/input-sink.hpp>
#include <sdl-rdp/freerdp-facade/touch-channel-events.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/picture/forward.hpp>
#include <sdl-rdp/utilities/geometry.hpp>

#include <oxbox/utilities/utf-decode.hpp>
#include <array>
#include <concepts>
#include <cstdint>
#include <span>

namespace sdl_rdp::input::detail::events {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::diagnostics::LoggedFailures;
using sdl_rdp::freerdp_facade::AdvancedInputChannelEvents;
using sdl_rdp::freerdp_facade::AdvancedPointerEvent;
using sdl_rdp::freerdp_facade::InputSink;
using sdl_rdp::freerdp_facade::KeyEvent;
using sdl_rdp::freerdp_facade::PointerEvent;
using sdl_rdp::freerdp_facade::TouchChannelEvents;
using sdl_rdp::freerdp_facade::TouchContact;
using sdl_rdp::freerdp_facade::UnicodeEvent;
using sdl_rdp::link::Activation;
using sdl_rdp::link::EventQueue;
using sdl_rdp::link::PeerLink;
using sdl_rdp::link::SessionAccess;
using sdl_rdp::picture::DesktopLayout;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::utilities::Rect;

enum class MouseMode{ Absolute, Relative };
struct MouseState {
  MouseMode mode          { MouseMode::Absolute };
  bool      have_relative { };
  bool      warp_requested{ };
  int       last_x        { };
  int       last_y        { };
};
class InputEvents final : public LoggedFailures<InputSink, TouchChannelEvents, AdvancedInputChannelEvents> {
public:
       InputEvents(PeerLink& link, Activation const& activation, DesktopLayout const& desktop, EventQueue& events,
                   FrameStore& store, Diagnostics const& diagnostics, SessionAccess& session) noexcept;
  auto Key(KeyEvent event)                                -> void override;
  auto Unicode(UnicodeEvent event)                        -> void override;
  auto Pointer(PointerEvent const& event)                 -> bool override;
  auto AdvancedPointer(AdvancedPointerEvent const& event) -> bool override;
  auto Touch(std::span<TouchContact const> contacts)      -> void override;
  auto Point(MouseMode mode) noexcept                     -> void;

private:
  template <class Result> auto WhenActive(Result idle, std::invocable auto action) -> Result;
  auto                         WhenActive(std::invocable auto action)              -> void;
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
  SessionAccess&                                  _session;
  std::array<oxbox::utilities::UtfDecodeState, 2> _unicode   { };
  MouseState                                      _mouse     { };
};
}

namespace sdl_rdp::input {
using detail::events::InputEvents;
using detail::events::MouseMode;
}
