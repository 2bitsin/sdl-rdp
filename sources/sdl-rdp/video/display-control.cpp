#include <sdl-rdp/video/display-control.hpp>

#include <sdl-rdp/core/activation.hpp>
#include <sdl-rdp/core/desktop-layout.hpp>
#include <sdl-rdp/core/event-queue.hpp>
#include <sdl-rdp/core/peer-link.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>

#include <freerdp/channels/wtsvc.h>
#include <freerdp/settings.h>
#include <algorithm>
#include <concepts>
#include <cstdint>
#include <ranges>
#include <span>

namespace Backend {
namespace {
constexpr UINT32 MonitorLimit      = 16;
constexpr UINT32 MonitorAreaFactor = 8192;
using Monitor = DISPLAY_CONTROL_MONITOR_LAYOUT;
auto Held(DispServerContext* context) -> DisplayControl& {
  Expects(context != nullptr, "callback context exists");
  return CallbackOwner<DisplayControl>(context->custom);
}
auto Layout(DispServerContext* context, DISPLAY_CONTROL_MONITOR_LAYOUT_PDU const* pdu) -> UINT {
  Expects(pdu, "display layout PDU is supplied");
  return Held(context).Layout(*pdu);
}
auto Edge(std::span<Monitor const> monitors, std::regular_invocable<Monitor const&> auto edge,
          std::regular_invocable<int64_t, int64_t> auto pick) -> int64_t {
  return std::ranges::fold_left(monitors | std::views::transform(edge), int64_t{ 0 }, pick);
}
auto Covering(std::span<Monitor const> monitors) -> sdlrdp_rect {
  auto const lower  = [](int64_t a, int64_t b) { return std::min(a, b); };
  auto const upper  = [](int64_t a, int64_t b) { return std::max(a, b); };
  auto const left   = Edge(monitors, [](auto const& m) { return int64_t(m.Left); }, lower);
  auto const top    = Edge(monitors, [](auto const& m) { return int64_t(m.Top); }, lower);
  auto const right  = Edge(monitors, [](auto const& m) { return int64_t(m.Left) + m.Width; }, upper);
  auto const bottom = Edge(monitors, [](auto const& m) { return int64_t(m.Top) + m.Height; }, upper);
  return { int(left), int(top), int(right - left), int(bottom - top) };
}
}
DisplayControl::DisplayControl(PeerLink& link, Activation const& activation, DesktopLayout const& desktop,
                               EventQueue& events) noexcept
    : _link{ link }, _activation{ activation }, _desktop{ desktop }, _events{ events }, _slot{ link.Dynamic(), *this } {
}
auto DisplayControl::Open() -> bool {
  if (_open || !freerdp_settings_get_bool(&_link.Settings(), FreeRDP_SupportDisplayControl)
      || !DynamicChannelsReady(_link))
    return true;
  _context.reset(disp_server_context_new(_link.Channels()));
  if (!BindContext(_context.get(), this, _link.Context())) return false;
  _context->DispMonitorLayout     = Backend::Layout;
  // abi: psDispChannelIdAssigned
  _context->ChannelIdAssigned     = [](DispServerContext* assigned, UINT32 channel_id) noexcept -> BOOL {
    Held(assigned)._slot.Assign(channel_id);
    return true;
  };
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
auto DisplayControl::Layout(DISPLAY_CONTROL_MONITOR_LAYOUT_PDU const& pdu) -> UINT {
  if (!pdu.NumMonitors || !_activation.Active()) return CHANNEL_RC_OK;
  auto const extent = Covering({ pdu.Monitors, pdu.NumMonitors });
  if (_desktop.Matches(extent)) return CHANNEL_RC_OK;
  _events.Push({ .type = SDLRDP_SCREEN, .screen = { .width = unsigned(extent.w), .height = unsigned(extent.h) } });
  return CHANNEL_RC_OK;
}
}
