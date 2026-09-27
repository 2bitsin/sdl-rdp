#include <sdl-rdp/video/display-control.hpp>

#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/picture/desktop-layout.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/channels/wtsvc.h>
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <ranges>
#include <span>

namespace sdl_rdp::video::detail::display_control {
using sdl_rdp::diagnostics::FailuresThrough;
using sdl_rdp::freerdp_facade::BindContext;
using sdl_rdp::freerdp_facade::BoolKey;
using sdl_rdp::freerdp_facade::CallbackOwner;
using sdl_rdp::link::DynamicChannelsReady;
using sdl_rdp::link::ScreenChanged;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::OperationName;
using sdl_rdp::utilities::Rect;

namespace {
constexpr std::uint32_t MonitorLimit      = 16;
constexpr std::uint32_t MonitorAreaFactor = 8192;
using Monitor = DISPLAY_CONTROL_MONITOR_LAYOUT;
auto Held(DispServerContext const& context) -> DisplayControl& {
  return CallbackOwner<DisplayControl, &DispServerContext::custom>(context);
}
constexpr OperationName DisplayLayout    { "Display layout"             };
constexpr OperationName DisplayAssignment{ "Display channel assignment" };
using sdl_rdp::freerdp_facade::Handled;
auto Edge(std::span<Monitor const> monitors, std::regular_invocable<Monitor const&> auto edge,
          std::regular_invocable<std::int64_t, std::int64_t> auto pick) -> std::int64_t {
  return std::ranges::fold_left(monitors | std::views::transform(edge), std::int64_t{ 0 }, pick);
}
auto Covering(std::span<Monitor const> monitors) -> Rect {
  auto const lower  = [](std::int64_t a, std::int64_t b) { return std::min(a, b); };
  auto const upper  = [](std::int64_t a, std::int64_t b) { return std::max(a, b); };
  auto const left   = Edge(monitors, [](auto const& m) { return std::int64_t{ m.Left }; }, lower);
  auto const top    = Edge(monitors, [](auto const& m) { return std::int64_t{ m.Top }; }, lower);
  auto const right  = Edge(monitors, [](auto const& m) { return std::int64_t{ m.Left } + m.Width; }, upper);
  auto const bottom = Edge(monitors, [](auto const& m) { return std::int64_t{ m.Top } + m.Height; }, upper);
  return { .x = Narrowed<int>(left),
           .y = Narrowed<int>(top),
           .w = Narrowed<int>(right - left),
           .h = Narrowed<int>(bottom - top) };
}
}
class DisplayControl::Callbacks {
public:
  static auto Install(DispServerContext& server) -> void;
};
auto DisplayControl::Callbacks::Install(DispServerContext& server) -> void {
  constexpr auto failures = FailuresThrough<&DisplayControl::FailureSource>;
  // abi: psDispMonitorLayout, UINT is uint32_t; psDispChannelIdAssigned, BOOL is int
  server.DispMonitorLayout = Handled<Held, &DisplayControl::Layout, DisplayLayout, failures, ERROR_INTERNAL_ERROR>;
  server.ChannelIdAssigned = Handled<Held, &DisplayControl::Assign, DisplayAssignment, failures, false>;
}
DisplayControl::DisplayControl(PeerLink& link, Activation const& activation, DesktopLayout const& desktop,
                               EventQueue& events, Diagnostics const& diagnostics) noexcept
    : _link{ link }, _activation{ activation }, _desktop{ desktop }, _events{ events }, _diagnostics{ diagnostics } { }
auto DisplayControl::Open() -> bool {
  if (_open || !_link.Settings().Get(BoolKey::SupportDisplayControl) || !DynamicChannelsReady(_link)) return true;
  _context.reset(disp_server_context_new(_link.Channels().get()));
  if (!_context) return false;
  BindContext(*_context, *this, _link.Context());
  Callbacks::Install(*_context);
  _context->MaxNumMonitors        = MonitorLimit;
  _context->MaxMonitorAreaFactorA = _context->MaxMonitorAreaFactorB = MonitorAreaFactor;
  _open                           = _context->Open(_context.get()) == CHANNEL_RC_OK;
  return _open;
}
auto DisplayControl::Opened() const noexcept -> std::optional<std::reference_wrapper<DispServerContext>> {
  if (!_open) return std::nullopt;
  return std::ref(*_context);
}
auto DisplayControl::Activate() -> bool {
  Expects(_context != nullptr, "an activated display channel is open");
  return _context->DisplayControlCaps(_context.get()) == CHANNEL_RC_OK;
}
auto DisplayControl::Reject() -> void { }
auto DisplayControl::Assign(std::uint32_t id) -> bool {
  _assignment.emplace(_link.Dynamic().Assign(id, *this));
  return true;
}
auto DisplayControl::Layout(DISPLAY_CONTROL_MONITOR_LAYOUT_PDU const& pdu) -> std::uint32_t {
  if (!pdu.NumMonitors || !_activation.Active()) return CHANNEL_RC_OK;
  auto const extent = Covering({ pdu.Monitors, pdu.NumMonitors });
  if (_desktop.Matches(extent)) return CHANNEL_RC_OK;
  if (extent.w <= 0 || extent.h <= 0) return ERROR_INVALID_DATA;
  _events.Push(
      ScreenChanged{ .width = Narrowed<std::uint32_t>(extent.w), .height = Narrowed<std::uint32_t>(extent.h) });
  return CHANNEL_RC_OK;
}
auto DisplayControl::FailureSource() const noexcept -> Diagnostics const& {
  return _diagnostics;
}
}
