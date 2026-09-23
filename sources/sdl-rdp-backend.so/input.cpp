#include "_detail/input.hpp"

#include "_detail/input-dispatch.hpp"

#include <array>
#include <freerdp/input.h>

namespace Backend {
BOOL Peer::Keyboard(rdpInput* input, UINT16 flags, UINT8 code) {
  return DispatchInput(input, [&](Peer& self) {
    self.owner.trace.Line("key", [&] {
      return std::format("code={} extended={} down={}", code, int(!!(flags & KBD_FLAGS_EXTENDED)),
                         int(!(flags & KBD_FLAGS_RELEASE)));
    });
    self.owner.Push({ .type = SDLRDP_KEY,
                      .key  = { .scancode = code,
                                .extended = !!(flags & KBD_FLAGS_EXTENDED),
                                .down     = !(flags & KBD_FLAGS_RELEASE) } });
    return TRUE;
  });
}
namespace {
void PushMouseButtons(State& owner, UINT16 flags) {
  constexpr std::array<unsigned, 3> buttons{ PTR_FLAGS_BUTTON1, PTR_FLAGS_BUTTON3, PTR_FLAGS_BUTTON2 };
  for (unsigned i = 0; i < buttons.size(); ++i)
    if (flags & buttons[i])
      owner.Push(
          { .type = SDLRDP_MOUSE_BUTTON, .mouse_button = { .button = i + 1, .down = !!(flags & PTR_FLAGS_DOWN) } });
}
void PushMouseWheel(State& owner, UINT16 flags) {
  if (flags & (PTR_FLAGS_WHEEL | PTR_FLAGS_HWHEEL)) {
    int rotation = flags & WheelRotationMask;
    if (flags & PTR_FLAGS_WHEEL_NEGATIVE) rotation -= 0x200;
    float const notches = float(rotation) / 120.0f;
    owner.Push({ .type        = SDLRDP_MOUSE_WHEEL,
                 .mouse_wheel = { .dx = (flags & PTR_FLAGS_HWHEEL) ? notches : 0,
                                  .dy = (flags & PTR_FLAGS_WHEEL) ? notches : 0 } });
  }
}
}
BOOL Peer::Mouse(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y) {
  return DispatchInput(input, [&](Peer& self) {
    auto& owner = self.owner;
    if (flags & (PTR_FLAGS_BUTTON1 | PTR_FLAGS_BUTTON2 | PTR_FLAGS_BUTTON3))
      owner.trace.Line("mouse", [&] { return std::format("flags={} x={} y={}", flags, x, y); });
    if ((flags & PTR_FLAGS_MOVE) && !Input::Motion(self, x, y)) return FALSE;
    PushMouseButtons(owner, flags);
    PushMouseWheel(owner, flags);
    return TRUE;
  });
}
BOOL Peer::ExtendedMouse(rdpInput* input, UINT16 flags, UINT16 /*unused*/, UINT16 /*unused*/) {
  return DispatchInput(input, [&](Peer& self) {
    auto&                             owner   = self.owner;
    constexpr std::array<unsigned, 2> buttons{ PTR_XFLAGS_BUTTON1, PTR_XFLAGS_BUTTON2 };
    for (unsigned i = 0; i < buttons.size(); ++i)
      if (flags & buttons[i])
        owner.Push(
            { .type = SDLRDP_MOUSE_BUTTON, .mouse_button = { .button = i + 4, .down = !!(flags & PTR_XFLAGS_DOWN) } });
    return TRUE;
  });
}
}
