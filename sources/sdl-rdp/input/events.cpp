#include <sdl-rdp/input/events.hpp>

#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/link/session-access.hpp>
#include <sdl-rdp/picture/desktop-layout.hpp>
#include <sdl-rdp/picture/frame-store.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/input.h>
#include <freerdp/server/ainput.h>
#include <algorithm>
#include <array>
#include <concepts>
#include <cstdint>
#include <format>
#include <functional>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>

namespace sdl_rdp::input::detail::events {
using sdl_rdp::freerdp_facade::CallbackOwner;
using sdl_rdp::picture::FrameLock;
using sdl_rdp::picture::Rescale;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;

template <class Result> auto InputEvents::WhenActive(Result idle, std::invocable auto action) -> Result {
  auto const session = _session.Lock();
  return _activation.Active() ? Result{ action() } : idle;
}
namespace {
template <std::unsigned_integral Flags>
auto PushButtons(EventQueue& events, std::span<Flags const> buttons, Flags flags, std::uint32_t first, bool down)
    -> void {
  for (auto const [index, button] : std::views::enumerate(buttons))
    if (flags & button)
      events.Push({ .type         = SDLRDP_MOUSE_BUTTON,
                    .mouse_button = { .button = first + Narrowed<std::uint32_t>(index), .down = down } });
}
constexpr std::uint32_t                FirstButton         = 1;
constexpr std::uint32_t                FirstExtendedButton = 4;
constexpr int                          WheelSignExtension  = 0x200;
constexpr float                        WheelNotch          = 120.0F;
constexpr std::array<std::uint16_t, 3> PointerButtons      { PTR_FLAGS_BUTTON1, PTR_FLAGS_BUTTON3, PTR_FLAGS_BUTTON2 };
constexpr std::array<std::uint16_t, 2> ExtendedButtons     { PTR_XFLAGS_BUTTON1, PTR_XFLAGS_BUTTON2                  };
auto PushMouseWheel(EventQueue& events, std::uint16_t flags) -> void {
  if (!(flags & (PTR_FLAGS_WHEEL | PTR_FLAGS_HWHEEL))) return;
  int rotation = flags & WheelRotationMask;
  if (flags & PTR_FLAGS_WHEEL_NEGATIVE) rotation -= WheelSignExtension;
  float const notches = static_cast<float>(rotation) / WheelNotch;
  events.Push({ .type        = SDLRDP_MOUSE_WHEEL,
                .mouse_wheel = { .dx = (flags & PTR_FLAGS_HWHEEL) ? notches : 0,
                                 .dy = (flags & PTR_FLAGS_WHEEL) ? notches : 0 } });
}
auto Owner(rdpInput const& input) -> InputEvents& {
  return CallbackOwner<InputEvents, &rdpInput::param1>(input);
}
constexpr OperationName KeyboardEvent       { "Keyboard event"         };
constexpr OperationName UnicodeKeyboardEvent{ "Unicode keyboard event" };
constexpr OperationName MouseEvent          { "Mouse event"            };
constexpr OperationName ExtendedMouseEvent  { "Extended mouse event"   };
using sdl_rdp::freerdp_facade::Handled;
}
InputEvents::InputEvents(PeerLink& link, Activation const& activation, DesktopLayout const& desktop, EventQueue& events,
                         FrameStore& store, Diagnostics const& diagnostics, SessionAccess& session) noexcept
    : _link{ link }, _activation{ activation }, _desktop{ desktop }, _events{ events }, _store{ store },
      _diagnostics{ diagnostics }, _session{ session } { }
auto InputEvents::Failures(OperationName operation) const noexcept -> FailureLog {
  return { _diagnostics, operation };
}
auto InputEvents::Install(rdpInput& input) -> void {
  input.param1 = this;
  // abi: pKeyboardEvent, pUnicodeKeyboardEvent, pMouseEvent, pExtendedMouseEvent; BOOL is int
  constexpr auto failures = &InputEvents::Failures;
  input.KeyboardEvent        = Handled<Owner, &InputEvents::Key, KeyboardEvent, failures, false>;
  input.UnicodeKeyboardEvent = Handled<Owner, &InputEvents::Text, UnicodeKeyboardEvent, failures, false>;
  input.MouseEvent           = Handled<Owner, &InputEvents::Mouse, MouseEvent, failures, false>;
  input.ExtendedMouseEvent   = Handled<Owner, &InputEvents::ExtendedMouse, ExtendedMouseEvent, failures, false>;
}
auto InputEvents::Key(std::uint16_t flags, std::uint8_t code) -> bool {
  return WhenActive(true, [&] {
    bool const extended = flags & KBD_FLAGS_EXTENDED;
    bool const down     = !(flags & KBD_FLAGS_RELEASE);
    _diagnostics.Line("key",
                      [&] { return std::format("code={} extended={} down={}", code, int{ extended }, int{ down }); });
    _events.Push({ .type = SDLRDP_KEY, .key = { .scancode = code, .extended = extended, .down = down } });
    return true;
  });
}
auto InputEvents::Text(std::uint16_t flags, std::uint16_t code) -> bool {
  return WhenActive(true, [&] {
    bool const down  = !(flags & KBD_FLAGS_RELEASE);
    auto const point = oxbox::utilities::UtfDecode(_unicode[down], code);
    if (!point || *point == oxbox::utilities::INVALID_CODEPOINT<>) return true;
    _diagnostics.Line("key", [&] { return std::format("codepoint={} down={}", std::uint32_t{ *point }, int{ down }); });
    _events.Push({ .type = SDLRDP_TEXT, .text = { .codepoint = *point, .down = down } });
    return true;
  });
}
auto InputEvents::Mouse(std::uint16_t flags, std::uint16_t x, std::uint16_t y) -> bool {
  return WhenActive(true, [&] {
    if (flags & (PTR_FLAGS_BUTTON1 | PTR_FLAGS_BUTTON2 | PTR_FLAGS_BUTTON3))
      _diagnostics.Line("mouse", [&] { return std::format("flags={} x={} y={}", flags, x, y); });
    if ((flags & PTR_FLAGS_MOVE) && !Motion(x, y)) return false;
    PushButtons<std::uint16_t>(_events, PointerButtons, flags, FirstButton, flags & PTR_FLAGS_DOWN);
    PushMouseWheel(_events, flags);
    return true;
  });
}
auto InputEvents::ExtendedMouse(std::uint16_t flags) -> bool {
  return WhenActive(true, [&] {
    PushButtons<std::uint16_t>(_events, ExtendedButtons, flags, FirstExtendedButton, flags & PTR_XFLAGS_DOWN);
    return true;
  });
}
namespace {
constexpr int                          EdgeFraction  = 8;
constexpr float                        WheelUnit     = 120.0F * 65536;
constexpr std::array<std::uint64_t, 5> AinputButtons { AINPUT_FLAGS_BUTTON1, AINPUT_FLAGS_BUTTON3, AINPUT_FLAGS_BUTTON2,
                                                       AINPUT_XFLAGS_BUTTON1, AINPUT_XFLAGS_BUTTON2 };
auto Outside(int value, int extent) -> bool {
  return value < extent / EdgeFraction || value >= extent * (EdgeFraction - 1) / EdgeFraction;
}
auto NearEdge(sdlrdp_rect desktop, int x, int y) -> bool {
  return Outside(x, desktop.w) || Outside(y, desktop.h);
}
auto AtCenter(sdlrdp_rect desktop, int x, int y) -> bool {
  return x == desktop.w / 2 && y == desktop.h / 2;
}
auto RelativeMotion(int dx, int dy, sdlrdp_rect /*bounds*/) -> sdlrdp_event {
  return { .type = SDLRDP_MOUSE_RELATIVE, .mouse_relative = { .dx = dx, .dy = dy } };
}
auto AbsoluteMotion(int x, int y, sdlrdp_rect bounds) -> sdlrdp_event {
  return { .type       = SDLRDP_MOUSE_MOVE,
           .mouse_move = { .x = std::clamp(x, 0, bounds.w - 1), .y = std::clamp(y, 0, bounds.h - 1) } };
}
}
auto InputEvents::Scaled(int x, int y, std::invocable<int, int, sdlrdp_rect> auto build) -> void {
  auto const bounds  = _store.Read([](FrameStore const& store, FrameLock const& held) { return store.Bounds(held); });
  auto const desktop = _desktop.Rect();
  _events.Push(build(Rescale(x, desktop.w, bounds.w), Rescale(y, desktop.h, bounds.h), bounds));
}
auto InputEvents::Point(MouseMode mode) noexcept -> void {
  _mouse.mode           = mode;
  _mouse.warp_requested = false;
}
auto InputEvents::Center() -> bool {
  Expects(_activation.Active(), "active peer has a desktop");
  auto const                    desktop  = _desktop.Rect();
  auto&                         context  = _link.Context();
  POINTER_POSITION_UPDATE const position { Narrowed<std::uint32_t>(desktop.w / 2),
                                           Narrowed<std::uint32_t>(desktop.h / 2) };
  _mouse.warp_requested = context.update->pointer->PointerPosition(&context, &position);
  return _mouse.warp_requested;
}
auto InputEvents::Motion(int x, int y) -> bool {
  auto const desktop = _desktop.Rect();
  Expects(desktop.w > 0, "desktop width is positive");
  Expects(desktop.h > 0, "desktop height is positive");
  int const dx = x - std::exchange(_mouse.last_x, x);
  int const dy = y - std::exchange(_mouse.last_y, y);
  if (_mouse.mode == MouseMode::Absolute) {
    Scaled(x, y, AbsoluteMotion);
    return true;
  }
  if (_mouse.have_relative) return true;
  bool const warped = std::exchange(_mouse.warp_requested, false) && AtCenter(desktop, x, y);
  if (!warped && (dx || dy)) Scaled(dx, dy, RelativeMotion);
  return NearEdge(desktop, x, y) ? Center() : true;
}
auto InputEvents::Pointer(std::uint64_t flags, std::int32_t x, std::int32_t y) -> std::uint32_t {
  return WhenActive(std::uint32_t{ CHANNEL_RC_OK }, [&] {
    bool const moved   = flags & AINPUT_FLAGS_MOVE;
    bool const shifted = moved && (flags & AINPUT_FLAGS_REL);
    if (moved) _mouse.have_relative = (flags & (AINPUT_FLAGS_REL | AINPUT_FLAGS_HAVE_REL)) != 0;
    if (_mouse.have_relative) _mouse.warp_requested = false;
    if (shifted && _mouse.mode == MouseMode::Relative) Scaled(x, y, RelativeMotion);
    if (moved && !shifted && !Motion(x, y)) return std::uint32_t{ ERROR_INTERNAL_ERROR };
    PushButtons<std::uint64_t>(_events, AinputButtons, flags, FirstButton, flags & AINPUT_FLAGS_DOWN);
    if (flags & AINPUT_FLAGS_WHEEL)
      _events.Push(
          { .type        = SDLRDP_MOUSE_WHEEL,
            .mouse_wheel = { .dx = static_cast<float>(x) / WheelUnit, .dy = static_cast<float>(y) / WheelUnit } });
    return std::uint32_t{ CHANNEL_RC_OK };
  });
}
namespace {
constexpr std::uint32_t PressureScale = 1024;
auto Phase(std::uint32_t flags) -> sdlrdp_touch_phase {
  if (flags & RDPINPUT_CONTACT_FLAG_CANCELED) return SDLRDP_TOUCH_CANCEL;
  if (flags & RDPINPUT_CONTACT_FLAG_UP) return SDLRDP_TOUCH_UP;
  if (flags & RDPINPUT_CONTACT_FLAG_DOWN) return SDLRDP_TOUCH_DOWN;
  return SDLRDP_TOUCH_MOVE;
}
auto Pressure(RDPINPUT_CONTACT_DATA const& contact) -> float {
  if (!(contact.fieldsPresent & CONTACT_DATA_PRESSURE_PRESENT)) return 1.0F;
  return static_cast<float>(std::min(contact.pressure, PressureScale)) / static_cast<float>(PressureScale);
}
auto Unit(std::int32_t value, int extent) -> float {
  return std::clamp(static_cast<float>(value) / static_cast<float>(extent), 0.0F, 1.0F);
}
auto Contact(sdlrdp_rect desktop, RDPINPUT_CONTACT_DATA const& contact) -> sdlrdp_event {
  Expects(desktop.w > 0, "desktop width is positive");
  Expects(desktop.h > 0, "desktop height is positive");
  return { .type  = SDLRDP_TOUCH,
           .touch = { .id       = contact.contactId,
                      .x        = Unit(contact.x, desktop.w),
                      .y        = Unit(contact.y, desktop.h),
                      .pressure = Pressure(contact),
                      .phase    = Phase(contact.contactFlags) } };
}
auto Contacts(RDPINPUT_TOUCH_EVENT const& event) {
  return std::span(event.frames, event.frameCount) | std::views::transform([](RDPINPUT_TOUCH_FRAME const& frame) {
           return std::span(frame.contacts, frame.contactCount);
         })
         | std::views::join;
}
}
auto InputEvents::Touch(RDPINPUT_TOUCH_EVENT const& event) -> std::uint32_t {
  return WhenActive(std::uint32_t{ CHANNEL_RC_OK }, [&] {
    for (auto const& contact : Contacts(event)) _events.Push(Contact(_desktop.Rect(), contact));
    return std::uint32_t{ CHANNEL_RC_OK };
  });
}
}
