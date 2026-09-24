#include "_detail/input-dispatch.hpp"
#include <sdl-rdp/core/desktop-layout.hpp>
#include <sdl-rdp/core/frame-store.hpp>
#include <sdl-rdp/core/peer-link.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/server/ainput.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

namespace Backend {
namespace {
constexpr std::uint32_t                FirstButton  = 1;
constexpr int                          EdgeFraction = 8;
constexpr float                        WheelUnit    = 120.0F * 65536;
constexpr std::array<std::uint64_t, 5> Buttons      { AINPUT_FLAGS_BUTTON1, AINPUT_FLAGS_BUTTON3, AINPUT_FLAGS_BUTTON2,
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
  auto const bounds = _store.Read([](FrameStore const& store, FrameLock const& held) { return store.Bounds(held); });
  _events.Push(
      build(_desktop.Scale(x, bounds.w, &sdlrdp_rect::w), _desktop.Scale(y, bounds.h, &sdlrdp_rect::h), bounds));
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
    PushButtons<std::uint64_t>(_events, Buttons, flags, FirstButton, flags & AINPUT_FLAGS_DOWN);
    if (flags & AINPUT_FLAGS_WHEEL)
      _events.Push(
          { .type = SDLRDP_MOUSE_WHEEL, .mouse_wheel = { .dx = float(x) / WheelUnit, .dy = float(y) / WheelUnit } });
    return std::uint32_t{ CHANNEL_RC_OK };
  });
}
}
