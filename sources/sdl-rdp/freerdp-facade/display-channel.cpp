#include <sdl-rdp/freerdp-facade/display-channel.hpp>

#include <sdl-rdp/freerdp-facade/assignment-sink.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>

#include <freerdp/server/disp.h>
#include <cstdint>
#include <ranges>
#include <span>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::display_channel {
using sdl_rdp::freerdp_facade::DisplayMonitor;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::OperationName;

namespace {
constexpr OperationName DisplayLayout     { "Display layout"             };
constexpr OperationName DisplayAssignment { "Display channel assignment" };
constexpr auto          UserData          = &DispServerContext::custom;
using Owner = DisplayChannelEvents;
auto Events(DispServerContext const& context) -> DisplayChannelEvents& {
  return CallbackOwner<Owner, UserData>(context);
}
auto Monitor(DISPLAY_CONTROL_MONITOR_LAYOUT const& monitor) -> DisplayMonitor {
  return { .left = monitor.Left, .top = monitor.Top, .width = monitor.Width, .height = monitor.Height };
}
auto Monitors(DISPLAY_CONTROL_MONITOR_LAYOUT_PDU const& pdu) -> std::vector<DisplayMonitor> {
  return std::span(pdu.Monitors, pdu.NumMonitors) | std::views::transform(Monitor) | std::ranges::to<std::vector>();
}
auto Layout(DisplayChannelEvents& events, DISPLAY_CONTROL_MONITOR_LAYOUT_PDU const& pdu) -> std::uint32_t {
  return events.MonitorLayout(Monitors(pdu)) ? CHANNEL_RC_OK : ERROR_INVALID_DATA;
}
auto InstallSlots(DispServerContext& context) -> void {
  // abi: psDispMonitorLayout, UINT is uint32_t; psDispChannelIdAssigned, BOOL is int
  context.DispMonitorLayout = Handled<Events, Layout, DisplayLayout, SinkFailures, ERROR_INTERNAL_ERROR>;
  context.ChannelIdAssigned = Handled<Events, Assigned, DisplayAssignment, SinkFailures, false>;
}
}
auto ReleaseDisplay(s_disp_server_context* context) noexcept -> void {
  disp_server_context_free(context);
}

DisplayChannel::DisplayChannel(ChannelManager& channels, DisplayChannelEvents& events) noexcept
    : _channels{ channels }, _events{ events } { }
auto DisplayChannel::Open(DisplayCaps caps) -> bool {
  Expects(_context == nullptr, "display control opens once");
  _context = _channels.Bound<DisplayContext, disp_server_context_new, UserData, InstallSlots, Owner>(_events);
  auto& context = *_context;
  context.MaxNumMonitors        = caps.max_monitors;
  context.MaxMonitorAreaFactorA = caps.max_area_factor_a;
  context.MaxMonitorAreaFactorB = caps.max_area_factor_b;
  return context.Open(&context) == CHANNEL_RC_OK;
}
auto DisplayChannel::Close() noexcept -> void {
  _context.reset();
}
auto DisplayChannel::Caps() -> bool {
  auto& context = Context();
  return context.DisplayControlCaps(&context) == CHANNEL_RC_OK;
}
auto DisplayChannel::Context() const -> s_disp_server_context& {
  Expects(_context != nullptr, "the display channel is open");
  return *_context;
}
}
