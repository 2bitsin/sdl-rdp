#include <sdl-rdp/video/display-control.hpp>

#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/picture/desktop-layout.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <ranges>
#include <span>

namespace sdl_rdp::video::detail::display_control {
using sdl_rdp::freerdp_facade::BoolKey;
using sdl_rdp::freerdp_facade::DisplayCaps;
using sdl_rdp::link::ScreenChanged;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Rect;

namespace {
constexpr DisplayCaps Limits{ .max_monitors = 16, .max_area_factor_a = 8192, .max_area_factor_b = 8192 };
auto Edge(std::span<DisplayMonitor const> monitors, std::regular_invocable<DisplayMonitor const&> auto edge,
          std::regular_invocable<std::int64_t, std::int64_t> auto pick) -> std::int64_t {
  return std::ranges::fold_left(monitors | std::views::transform(edge), std::int64_t{ 0 }, pick);
}
auto Covering(std::span<DisplayMonitor const> monitors) -> Rect {
  auto const lower  = [](std::int64_t a, std::int64_t b) { return std::min(a, b); };
  auto const upper  = [](std::int64_t a, std::int64_t b) { return std::max(a, b); };
  auto const left   = Edge(monitors, [](auto const& m) { return std::int64_t{ m.left }; }, lower);
  auto const top    = Edge(monitors, [](auto const& m) { return std::int64_t{ m.top }; }, lower);
  auto const right  = Edge(monitors, [](auto const& m) { return std::int64_t{ m.left } + m.width; }, upper);
  auto const bottom = Edge(monitors, [](auto const& m) { return std::int64_t{ m.top } + m.height; }, upper);
  return { .x = Narrowed<int>(left),
           .y = Narrowed<int>(top),
           .w = Narrowed<int>(right - left),
           .h = Narrowed<int>(bottom - top) };
}
}
DisplayControl::DisplayControl(PeerLink& link, Activation const& activation, DesktopLayout const& desktop,
                               EventQueue& events, Diagnostics const& diagnostics) noexcept
    : LoggedFailures{ diagnostics }, _link{ link }, _activation{ activation }, _desktop{ desktop }, _events{ events },
      _channel{ link.Channels(), *this } { }
auto DisplayControl::Open() -> bool {
  if (_open || !_link.Connection().Settings().Get(BoolKey::SupportDisplayControl) || !_link.Channels().DynamicReady())
    return true;
  _channel.Close();
  _open = _channel.Open(Limits);
  return _open;
}
auto DisplayControl::Opened() noexcept -> std::optional<std::reference_wrapper<DisplayChannelEvents>> {
  if (!_open) return std::nullopt;
  return std::ref<DisplayChannelEvents>(*this);
}
auto DisplayControl::Activate() -> bool {
  return _channel.Caps();
}
auto DisplayControl::Reject() -> void { }
auto DisplayControl::ChannelAssigned(std::uint32_t id) -> void {
  _assignment.emplace(_link.Dynamic().Assign(id, *this));
}
auto DisplayControl::MonitorLayout(std::span<DisplayMonitor const> monitors) -> bool {
  if (monitors.empty() || !_activation.Active()) return true;
  auto const extent = Covering(monitors);
  if (_desktop.Matches(extent)) return true;
  if (extent.w <= 0 || extent.h <= 0) return false;
  _events.Push(
      ScreenChanged{ .width = Narrowed<std::uint32_t>(extent.w), .height = Narrowed<std::uint32_t>(extent.h) });
  return true;
}
}
