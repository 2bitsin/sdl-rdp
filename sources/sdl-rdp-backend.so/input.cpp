#include "_detail/state.hpp"
#include <freerdp/input.h>
#include <array>

namespace Backend {
BOOL Peer::Keyboard(rdpInput* input, UINT16 flags, UINT8 code)
{
  Expects(input && input->context, "input context exists");
  auto& self = Held(input->context->peer);
  std::scoped_lock lock(self.owner.session_guard);
  if (!self.active) return TRUE;
  self.owner.Push({.type = SDLRDP_KEY,
    .key = {code, !!(flags & KBD_FLAGS_EXTENDED), !(flags & KBD_FLAGS_RELEASE)}});
  return TRUE;
}
BOOL Peer::Mouse(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y)
{
  Expects(input && input->context, "input context exists");
  auto& self = Held(input->context->peer);
  auto& owner = self.owner;
  std::scoped_lock lock(owner.session_guard);
  if (!self.active) return TRUE;
  if (flags & PTR_FLAGS_MOVE) owner.Push({.type = SDLRDP_MOUSE_MOVE, .mouse_move = {x, y}});
  constexpr std::array<unsigned, 3> buttons{PTR_FLAGS_BUTTON1, PTR_FLAGS_BUTTON3, PTR_FLAGS_BUTTON2};
  for (unsigned i = 0; i < buttons.size(); ++i)
    if (flags & buttons[i]) owner.Push({.type = SDLRDP_MOUSE_BUTTON,
      .mouse_button = {i + 1, !!(flags & PTR_FLAGS_DOWN)}});
  if (flags & (PTR_FLAGS_WHEEL | PTR_FLAGS_HWHEEL)) {
    int rotation = flags & WheelRotationMask;
    if (flags & PTR_FLAGS_WHEEL_NEGATIVE) rotation -= 0x200;
    int notches = rotation / 120;
    owner.Push({.type = SDLRDP_MOUSE_WHEEL,
      .mouse_wheel = {(flags & PTR_FLAGS_HWHEEL) ? notches : 0,
                      (flags & PTR_FLAGS_WHEEL) ? notches : 0}});
  }
  return TRUE;
}
BOOL Peer::ExtendedMouse(rdpInput* input, UINT16 flags, UINT16, UINT16)
{
  Expects(input && input->context, "input context exists");
  auto& self = Held(input->context->peer);
  auto& owner = self.owner;
  std::scoped_lock lock(owner.session_guard);
  if (!self.active) return TRUE;
  constexpr std::array<unsigned, 2> buttons{PTR_XFLAGS_BUTTON1, PTR_XFLAGS_BUTTON2};
  for (unsigned i = 0; i < buttons.size(); ++i)
    if (flags & buttons[i]) owner.Push({.type = SDLRDP_MOUSE_BUTTON,
      .mouse_button = {i + 4, !!(flags & PTR_XFLAGS_DOWN)}});
  return TRUE;
}
}
