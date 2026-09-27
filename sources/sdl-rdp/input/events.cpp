#include <sdl-rdp/input/events.hpp>

#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/freerdp-facade/updates.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/link/session-access.hpp>
#include <sdl-rdp/picture/desktop-layout.hpp>
#include <sdl-rdp/picture/frame-store.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <algorithm>
#include <array>
#include <bitset>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>

namespace sdl_rdp::input::detail::events {
using sdl_rdp::freerdp_facade::ContactPhase;
using sdl_rdp::freerdp_facade::PointerButton;
using sdl_rdp::freerdp_facade::PointerButtonCount;
using sdl_rdp::freerdp_facade::Updates;
using sdl_rdp::link::Event;
using sdl_rdp::link::MouseButton;
using sdl_rdp::link::MouseMove;
using sdl_rdp::link::MouseRelative;
using sdl_rdp::link::MouseWheel;
using sdl_rdp::link::TextInput;
using sdl_rdp::link::Touch;
using sdl_rdp::link::TouchPhase;
using sdl_rdp::picture::FrameLock;
using sdl_rdp::picture::Rescale;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::Unreachable;

template <class Result> auto InputEvents::WhenActive(Result idle, std::invocable auto action) -> Result {
  auto const session = _session.Lock();
  return _activation.Active() ? Result{ action() } : idle;
}
auto InputEvents::WhenActive(std::invocable auto action) -> void {
  auto const session = _session.Lock();
  if (_activation.Active()) action();
}
namespace {
constexpr std::array AllButtons{ PointerButton::Left, PointerButton::Middle, PointerButton::Right, PointerButton::Back,
                                 PointerButton::Forward };
static_assert(AllButtons.size() == PointerButtonCount, "every pointer button has its link number");
// The link numbers mouse buttons from 1 in the order the driver's table reads them (SDL3/rdp/video/events.cpp).
auto LinkButton(PointerButton button) -> std::uint32_t {
  switch (button) {
  case PointerButton::Left:    return 1;
  case PointerButton::Middle:  return 2;
  case PointerButton::Right:   return 3;
  case PointerButton::Back:    return 4;
  case PointerButton::Forward: return 5;
  default:                     Unreachable(button);
  }
}
auto PushButtons(EventQueue& events, std::ranges::input_range auto&& pressed, bool down) -> void {
  for (auto const button : pressed) events.Push(MouseButton{ .button = LinkButton(button), .down = down });
}
auto Pressed(std::bitset<PointerButtonCount> const& buttons) -> auto {
  return AllButtons
         | std::views::filter([&buttons](PointerButton button) { return buttons.test(std::to_underlying(button)); });
}
template <class EventTy> auto PushPresses(EventQueue& events, EventTy const& event) -> void {
  PushButtons(events, Pressed(event.buttons), event.down);
  if (event.wheel) events.Push(MouseWheel{ .dx = event.wheel->horizontal, .dy = event.wheel->vertical });
}
}
InputEvents::InputEvents(PeerLink& link, Activation const& activation, DesktopLayout const& desktop, EventQueue& events,
                         FrameStore& store, Diagnostics const& diagnostics, SessionAccess& session) noexcept
    : LoggedFailures{ diagnostics }, _link{ link }, _activation{ activation }, _desktop{ desktop }, _events{ events },
      _store{ store }, _session{ session } { }
auto InputEvents::Key(KeyEvent event) -> void {
  WhenActive([&] {
    Logger().Line("key", [&] {
      return std::format("code={} extended={} down={}", event.code, int{ event.extended }, int{ event.down });
    });
    _events.Push(sdl_rdp::link::Key{ .scancode = event.code, .extended = event.extended, .down = event.down });
  });
}
auto InputEvents::Unicode(UnicodeEvent event) -> void {
  WhenActive([&] {
    auto const point = oxbox::utilities::UtfDecode(_unicode[event.down], event.code);
    if (!point || *point == oxbox::utilities::INVALID_CODEPOINT<>) return;
    Logger().Line("key",
                  [&] { return std::format("codepoint={} down={}", std::uint32_t{ *point }, int{ event.down }); });
    _events.Push(TextInput{ .codepoint = *point, .down = event.down });
  });
}
auto InputEvents::Pointer(PointerEvent const& event) -> bool {
  return WhenActive(true, [&] {
    if (event.buttons.any())
      Logger().Line("mouse", [&] {
        return std::format("buttons={} down={} x={} y={}", event.buttons.to_string(), int{ event.down }, event.x,
                           event.y);
      });
    if (event.moved && !Motion(event.x, event.y)) return false;
    PushPresses(_events, event);
    return true;
  });
}
namespace {
constexpr int EdgeFraction = 8;
auto Outside(int value, int extent) -> bool {
  return value < extent / EdgeFraction || value >= extent * (EdgeFraction - 1) / EdgeFraction;
}
auto NearEdge(Rect desktop, int x, int y) -> bool {
  return Outside(x, desktop.w) || Outside(y, desktop.h);
}
auto AtCenter(Rect desktop, int x, int y) -> bool {
  return x == desktop.w / 2 && y == desktop.h / 2;
}
auto RelativeMotion(int dx, int dy, Rect /*bounds*/) -> Event {
  return MouseRelative{ .dx = dx, .dy = dy };
}
auto AbsoluteMotion(int x, int y, Rect bounds) -> Event {
  return MouseMove{ .x = std::clamp(x, 0, bounds.w - 1), .y = std::clamp(y, 0, bounds.h - 1) };
}
}
template <auto BUILD>
  requires std::invocable<decltype(BUILD), int, int, Rect>
auto InputEvents::Scaled(int x, int y) -> void {
  auto const bounds  = _store.Read([](FrameStore const& store, FrameLock const& held) { return store.Bounds(held); });
  auto const desktop = _desktop.Desktop();
  _events.Push(BUILD(Rescale(x, desktop.w, bounds.w), Rescale(y, desktop.h, bounds.h), bounds));
}
auto InputEvents::Point(MouseMode mode) noexcept -> void {
  _mouse.mode           = mode;
  _mouse.warp_requested = false;
}
auto InputEvents::Center() -> bool {
  Expects(_activation.Active(), "active peer has a desktop");
  auto const desktop = _desktop.Desktop();
  _mouse.warp_requested = Updates{ _link.Connection() }.MovePointer(Narrowed<std::uint32_t>(desktop.w / 2),
                                                                    Narrowed<std::uint32_t>(desktop.h / 2));
  return _mouse.warp_requested;
}
auto InputEvents::Motion(int x, int y) -> bool {
  auto const desktop = _desktop.Desktop();
  Expects(desktop.w > 0, "desktop width is positive");
  Expects(desktop.h > 0, "desktop height is positive");
  int const dx = x - std::exchange(_mouse.last_x, x);
  int const dy = y - std::exchange(_mouse.last_y, y);
  if (_mouse.mode == MouseMode::Absolute) {
    Scaled<AbsoluteMotion>(x, y);
    return true;
  }
  if (_mouse.have_relative) return true;
  bool const warped = std::exchange(_mouse.warp_requested, false) && AtCenter(desktop, x, y);
  if (!warped && (dx || dy)) Scaled<RelativeMotion>(dx, dy);
  return NearEdge(desktop, x, y) ? Center() : true;
}
auto InputEvents::AdvancedPointer(AdvancedPointerEvent const& event) -> bool {
  return WhenActive(true, [&] {
    bool const shifted = event.moved && event.relative;
    if (event.moved) _mouse.have_relative = event.relative_capable;
    if (_mouse.have_relative) _mouse.warp_requested = false;
    if (shifted && _mouse.mode == MouseMode::Relative) Scaled<RelativeMotion>(event.x, event.y);
    if (event.moved && !shifted && !Motion(event.x, event.y)) return false;
    PushPresses(_events, event);
    return true;
  });
}
namespace {
constexpr std::uint32_t PressureScale = 1024;
auto Phase(ContactPhase phase) -> TouchPhase {
  switch (phase) {
  case ContactPhase::Down:   return TouchPhase::Down;
  case ContactPhase::Move:   return TouchPhase::Move;
  case ContactPhase::Up:     return TouchPhase::Up;
  case ContactPhase::Cancel: return TouchPhase::Cancel;
  default:                   Unreachable(phase);
  }
}
auto Pressure(std::optional<std::uint32_t> pressure) -> float {
  if (!pressure) return 1.0F;
  return static_cast<float>(std::min(*pressure, PressureScale)) / static_cast<float>(PressureScale);
}
auto Unit(std::int32_t value, int extent) -> float {
  return std::clamp(static_cast<float>(value) / static_cast<float>(extent), 0.0F, 1.0F);
}
auto Contact(Rect desktop, TouchContact const& contact) -> Event {
  Expects(desktop.w > 0, "desktop width is positive");
  Expects(desktop.h > 0, "desktop height is positive");
  return Touch{ .id       = contact.id,
                .x        = Unit(contact.x, desktop.w),
                .y        = Unit(contact.y, desktop.h),
                .pressure = Pressure(contact.pressure),
                .phase    = Phase(contact.phase) };
}
}
auto InputEvents::Touch(std::span<TouchContact const> contacts) -> void {
  WhenActive([&] {
    for (auto const& contact : contacts) _events.Push(Contact(_desktop.Desktop(), contact));
  });
}
}
