#include "_detail/desktop-layout.hpp"
#include "_detail/frame-store.hpp"
#include "_detail/input-dispatch.hpp"
#include "_detail/peer-link.hpp"

#include <algorithm>
#include <array>
#include <freerdp/server/ainput.h>
#include <utility>

namespace Backend {
namespace {
constexpr unsigned              FirstButton  = 1;
constexpr int                   EdgeFraction = 8;
constexpr float                 WheelUnit    = 120.0F * 65536;
constexpr std::array<UINT64, 5> Buttons      { AINPUT_FLAGS_BUTTON1, AINPUT_FLAGS_BUTTON3, AINPUT_FLAGS_BUTTON2,
                                               AINPUT_XFLAGS_BUTTON1, AINPUT_XFLAGS_BUTTON2 };
bool Outside(int value, int extent) {
  return value < extent / EdgeFraction || value >= extent * (EdgeFraction - 1) / EdgeFraction;
}
bool NearEdge(sdlrdp_rect desktop, int x, int y) {
  return Outside(x, desktop.w) || Outside(y, desktop.h);
}
bool AtCenter(sdlrdp_rect desktop, int x, int y) {
  return x == desktop.w / 2 && y == desktop.h / 2;
}
sdlrdp_event RelativeMotion(int dx, int dy, sdlrdp_rect /*bounds*/) {
  return { .type = SDLRDP_MOUSE_RELATIVE, .mouse_relative = { .dx = dx, .dy = dy } };
}
sdlrdp_event AbsoluteMotion(int x, int y, sdlrdp_rect bounds) {
  return { .type       = SDLRDP_MOUSE_MOVE,
           .mouse_move = { .x = std::clamp(x, 0, bounds.w - 1), .y = std::clamp(y, 0, bounds.h - 1) } };
}
}
void InputEvents::Scaled(int x, int y, std::invocable<int, int, sdlrdp_rect> auto build) {
  auto const bounds = _store.Read([](FrameStore const& store, FrameLock const& held) { return store.Bounds(held); });
  _events.Push(build(_desktop.Scale(x, bounds.w, &sdlrdp_rect::w), _desktop.Scale(y, bounds.h, &sdlrdp_rect::h),
                     bounds));
}
void InputEvents::Point(MouseMode mode) noexcept {
  _mouse.mode           = mode;
  _mouse.warp_requested = false;
}
bool InputEvents::Center() {
  Expects(_activation.Active(), "active peer has a desktop");
  auto const                    desktop  = _desktop.Rect();
  auto&                         context  = _link.Context();
  POINTER_POSITION_UPDATE const position { UINT32(desktop.w / 2), UINT32(desktop.h / 2) };
  _mouse.warp_requested = context.update->pointer->PointerPosition(&context, &position);
  return _mouse.warp_requested;
}
bool InputEvents::Motion(int x, int y) {
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
UINT InputEvents::Pointer(UINT64 flags, INT32 x, INT32 y) {
  return WhenActive(UINT{ CHANNEL_RC_OK }, [&] {
    bool const moved   = flags & AINPUT_FLAGS_MOVE;
    bool const shifted = moved && (flags & AINPUT_FLAGS_REL);
    if (moved) _mouse.have_relative = (flags & (AINPUT_FLAGS_REL | AINPUT_FLAGS_HAVE_REL)) != 0;
    if (_mouse.have_relative) _mouse.warp_requested = false;
    if (shifted && _mouse.mode == MouseMode::Relative) Scaled(x, y, RelativeMotion);
    if (moved && !shifted && !Motion(x, y)) return UINT{ ERROR_INTERNAL_ERROR };
    PushButtons<UINT64>(_events, Buttons, flags, FirstButton, flags & AINPUT_FLAGS_DOWN);
    if (flags & AINPUT_FLAGS_WHEEL)
      _events.Push(
          { .type = SDLRDP_MOUSE_WHEEL, .mouse_wheel = { .dx = float(x) / WheelUnit, .dy = float(y) / WheelUnit } });
    return UINT{ CHANNEL_RC_OK };
  });
}
}
