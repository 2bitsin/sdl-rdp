#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/input/_detail/dispatch.hpp>

#include <freerdp/input.h>
#include <array>
#include <cstdint>
#include <format>
#include <functional>
#include <string_view>

namespace Backend {
namespace {
constexpr std::uint32_t                FirstButton         = 1;
constexpr std::uint32_t                FirstExtendedButton = 4;
constexpr int                          WheelSignExtension  = 0x200;
constexpr float                        WheelNotch          = 120.0F;
constexpr std::array<std::uint16_t, 3> Buttons             { PTR_FLAGS_BUTTON1, PTR_FLAGS_BUTTON3, PTR_FLAGS_BUTTON2 };
constexpr std::array<std::uint16_t, 2> ExtendedButtons     { PTR_XFLAGS_BUTTON1, PTR_XFLAGS_BUTTON2                  };
auto PushMouseWheel(EventQueue& events, std::uint16_t flags) -> void {
  if (!(flags & (PTR_FLAGS_WHEEL | PTR_FLAGS_HWHEEL))) return;
  int rotation = flags & WheelRotationMask;
  if (flags & PTR_FLAGS_WHEEL_NEGATIVE) rotation -= WheelSignExtension;
  float const notches = float(rotation) / WheelNotch;
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
                      [&] { return std::format("code={} extended={} down={}", code, int(extended), int(down)); });
    _events.Push({ .type = SDLRDP_KEY, .key = { .scancode = code, .extended = extended, .down = down } });
    return true;
  });
}
auto InputEvents::Text(std::uint16_t flags, std::uint16_t code) -> bool {
  return WhenActive(true, [&] {
    bool const down  = !(flags & KBD_FLAGS_RELEASE);
    auto const point = oxbox::utilities::UtfDecode(_unicode[down], code);
    if (!point || *point == oxbox::utilities::INVALID_CODEPOINT<>) return true;
    _diagnostics.Line("key", [&] { return std::format("codepoint={} down={}", std::uint32_t{ *point }, int(down)); });
    _events.Push({ .type = SDLRDP_TEXT, .text = { .codepoint = *point, .down = down } });
    return true;
  });
}
auto InputEvents::Mouse(std::uint16_t flags, std::uint16_t x, std::uint16_t y) -> bool {
  return WhenActive(true, [&] {
    if (flags & (PTR_FLAGS_BUTTON1 | PTR_FLAGS_BUTTON2 | PTR_FLAGS_BUTTON3))
      _diagnostics.Line("mouse", [&] { return std::format("flags={} x={} y={}", flags, x, y); });
    if ((flags & PTR_FLAGS_MOVE) && !Motion(x, y)) return false;
    PushButtons<std::uint16_t>(_events, Buttons, flags, FirstButton, flags & PTR_FLAGS_DOWN);
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
}
