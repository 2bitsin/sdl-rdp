#include <sdl-rdp/video/display-control.hpp>

#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/picture/desktop-layout.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/channels/wtsvc.h>
#include <freerdp/settings.h>
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <ranges>
#include <span>

namespace Backend {
namespace {
constexpr std::uint32_t MonitorLimit      = 16;
constexpr std::uint32_t MonitorAreaFactor = 8192;
using Monitor = DISPLAY_CONTROL_MONITOR_LAYOUT;
auto Held(DispServerContext* context) -> DisplayControl& {
  Expects(context != nullptr, "callback context exists");
  return CallbackOwner<DisplayControl>(context->custom);
}
auto Edge(std::span<Monitor const> monitors, std::regular_invocable<Monitor const&> auto edge,
          std::regular_invocable<std::int64_t, std::int64_t> auto pick) -> std::int64_t {
  return std::ranges::fold_left(monitors | std::views::transform(edge), std::int64_t{ 0 }, pick);
}
auto Covering(std::span<Monitor const> monitors) -> sdlrdp_rect {
  auto const lower  = [](std::int64_t a, std::int64_t b) { return std::min(a, b); };
  auto const upper  = [](std::int64_t a, std::int64_t b) { return std::max(a, b); };
  auto const left   = Edge(monitors, [](auto const& m) { return std::int64_t{ m.Left }; }, lower);
  auto const top    = Edge(monitors, [](auto const& m) { return std::int64_t{ m.Top }; }, lower);
  auto const right  = Edge(monitors, [](auto const& m) { return std::int64_t{ m.Left } + m.Width; }, upper);
  auto const bottom = Edge(monitors, [](auto const& m) { return std::int64_t{ m.Top } + m.Height; }, upper);
  return { int(left), int(top), int(right - left), int(bottom - top) };
}
}
class DisplayControl::Callbacks {
public:
  static auto Install(DispServerContext& server) -> void;
};
auto DisplayControl::Callbacks::Install(DispServerContext& server) -> void {
  // abi: psDispMonitorLayout, UINT is uint32_t
  server.DispMonitorLayout = [](DispServerContext* context,
                                DISPLAY_CONTROL_MONITOR_LAYOUT_PDU const* pdu) noexcept -> std::uint32_t {
    Expects(pdu != nullptr, "display layout PDU is supplied");
    auto&      owner    = Held(context);
    auto const laid_out = [&] { return owner.Layout(*pdu); };
    return Contained(ERROR_INTERNAL_ERROR, laid_out, FailureLog{ owner._diagnostics, "Display layout" });
  };
  // abi: psDispChannelIdAssigned, BOOL is int
  server.ChannelIdAssigned = [](DispServerContext* context, std::uint32_t channel_id) noexcept -> int {
    auto& owner = Held(context);
    return owner._slot.Assigned(channel_id, FailureLog{ owner._diagnostics, "Display channel assignment" });
  };
}
DisplayControl::DisplayControl(PeerLink& link, Activation const& activation, DesktopLayout const& desktop,
                               EventQueue& events, Diagnostics const& diagnostics) noexcept
    : _link{ link }, _activation{ activation }, _desktop{ desktop }, _events{ events }, _diagnostics{ diagnostics },
      _slot{ link.Dynamic(), *this } { }
auto DisplayControl::Open() -> bool {
  if (_open || !freerdp_settings_get_bool(&_link.Settings(), FreeRDP_SupportDisplayControl)
      || !DynamicChannelsReady(_link))
    return true;
  _context.reset(disp_server_context_new(_link.Channels()));
  if (!BindContext(_context.get(), this, _link.Context())) return false;
  Callbacks::Install(*_context);
  _context->MaxNumMonitors        = MonitorLimit;
  _context->MaxMonitorAreaFactorA = _context->MaxMonitorAreaFactorB = MonitorAreaFactor;
  _open                           = _context->Open(_context.get()) == CHANNEL_RC_OK;
  return _open;
}
auto DisplayControl::Opened() const noexcept -> DispServerContext* {
  return _open ? _context.get() : nullptr;
}
auto DisplayControl::Activate() -> bool {
  Expects(_context != nullptr, "an activated display channel is open");
  return _context->DisplayControlCaps(_context.get()) == CHANNEL_RC_OK;
}
auto DisplayControl::Reject() -> void { }
auto DisplayControl::Layout(DISPLAY_CONTROL_MONITOR_LAYOUT_PDU const& pdu) -> std::uint32_t {
  if (!pdu.NumMonitors || !_activation.Active()) return CHANNEL_RC_OK;
  auto const extent = Covering({ pdu.Monitors, pdu.NumMonitors });
  if (_desktop.Matches(extent)) return CHANNEL_RC_OK;
  if (extent.w <= 0 || extent.h <= 0) return ERROR_INVALID_DATA;
  _events.Push(
      { .type   = SDLRDP_SCREEN,
        .screen = { .width = Narrowed<std::uint32_t>(extent.w), .height = Narrowed<std::uint32_t>(extent.h) } });
  return CHANNEL_RC_OK;
}
}
