#include "_detail/callback-owner.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/input-dispatch.hpp"

#include <freerdp/input.h>
#include <array>
#include <format>

namespace Backend {
namespace {
constexpr unsigned              FirstButton         = 1;
constexpr unsigned              FirstExtendedButton = 4;
constexpr int                   WheelSignExtension  = 0x200;
constexpr float                 WheelNotch          = 120.0F;
constexpr std::array<UINT16, 3> Buttons             { PTR_FLAGS_BUTTON1, PTR_FLAGS_BUTTON3, PTR_FLAGS_BUTTON2 };
constexpr std::array<UINT16, 2> ExtendedButtons     { PTR_XFLAGS_BUTTON1, PTR_XFLAGS_BUTTON2                  };
auto PushMouseWheel(EventQueue& events, UINT16 flags) -> void {
  if (!(flags & (PTR_FLAGS_WHEEL | PTR_FLAGS_HWHEEL))) return;
  int rotation = flags & WheelRotationMask;
  if (flags & PTR_FLAGS_WHEEL_NEGATIVE) rotation -= WheelSignExtension;
  float const notches = float(rotation) / WheelNotch;
  events.Push({ .type        = SDLRDP_MOUSE_WHEEL,
                .mouse_wheel = { .dx = (flags & PTR_FLAGS_HWHEEL) ? notches : 0,
                                 .dy = (flags & PTR_FLAGS_WHEEL) ? notches : 0 } });
}
auto Owner(rdpInput* input) -> InputEvents& {
  Expects(input != nullptr, "input object exists");
  return CallbackOwner<InputEvents>(input->param1);
}
}
InputEvents::InputEvents(PeerLink& link, Activation const& activation, DesktopLayout const& desktop, EventQueue& events,
                         FrameStore& store, Diagnostics const& diagnostics, SessionAccess& session) noexcept
    : _link{ link }, _activation{ activation }, _desktop{ desktop }, _events{ events }, _store{ store },
      _diagnostics{ diagnostics }, _session{ session } { }
auto InputEvents::Install(rdpInput& input) -> void {
  input.param1        = this;
  input.KeyboardEvent = [](rdpInput* in, UINT16 flags, UINT8 code) { return Owner(in).Key(flags, code); };
  input.UnicodeKeyboardEvent = [](rdpInput* in, UINT16 flags, UINT16 code) { return Owner(in).Text(flags, code); };
  input.MouseEvent    = [](rdpInput* in, UINT16 flags, UINT16 x, UINT16 y) { return Owner(in).Mouse(flags, x, y); };
  input.ExtendedMouseEvent = [](rdpInput* in, UINT16 flags, UINT16, UINT16) { return Owner(in).ExtendedMouse(flags); };
}
auto InputEvents::Key(UINT16 flags, UINT8 code) -> BOOL {
  return WhenActive(BOOL{ TRUE }, [&] {
    bool const extended = flags & KBD_FLAGS_EXTENDED;
    bool const down     = !(flags & KBD_FLAGS_RELEASE);
    _diagnostics.Line("key",
                      [&] { return std::format("code={} extended={} down={}", code, int(extended), int(down)); });
    _events.Push({ .type = SDLRDP_KEY, .key = { .scancode = code, .extended = extended, .down = down } });
    return TRUE;
  });
}
auto InputEvents::Text(UINT16 flags, UINT16 code) -> BOOL {
  return WhenActive(BOOL{ TRUE }, [&] {
    bool const down  = !(flags & KBD_FLAGS_RELEASE);
    auto const point = oxbox::utilities::UtfDecode(_unicode[down], code);
    if (!point || *point == oxbox::utilities::INVALID_CODEPOINT<>) return TRUE;
    _diagnostics.Line("key", [&] { return std::format("codepoint={} down={}", uint32_t(*point), int(down)); });
    _events.Push({ .type = SDLRDP_TEXT, .text = { .codepoint = *point, .down = down } });
    return TRUE;
  });
}
auto InputEvents::Mouse(UINT16 flags, UINT16 x, UINT16 y) -> BOOL {
  return WhenActive(BOOL{ TRUE }, [&] {
    if (flags & (PTR_FLAGS_BUTTON1 | PTR_FLAGS_BUTTON2 | PTR_FLAGS_BUTTON3))
      _diagnostics.Line("mouse", [&] { return std::format("flags={} x={} y={}", flags, x, y); });
    if ((flags & PTR_FLAGS_MOVE) && !Motion(x, y)) return FALSE;
    PushButtons<UINT16>(_events, Buttons, flags, FirstButton, flags & PTR_FLAGS_DOWN);
    PushMouseWheel(_events, flags);
    return TRUE;
  });
}
auto InputEvents::ExtendedMouse(UINT16 flags) -> BOOL {
  return WhenActive(BOOL{ TRUE }, [&] {
    PushButtons<UINT16>(_events, ExtendedButtons, flags, FirstExtendedButton, flags & PTR_XFLAGS_DOWN);
    return TRUE;
  });
}
}
