#include <sdl-rdp/input/touch-protocol.hpp>

#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/input/events.hpp>
#include <sdl-rdp/utilities/contained.hpp>

#include <freerdp/channels/wtsvc.h>
#include <cstdint>
#include <utility>

namespace Backend {
namespace {
auto TouchHandled(std::uint32_t result) -> bool {
  switch (result) {
  case CHANNEL_RC_OK:
  // FreeRDP 3.32 channels/rdpei/server/rdpei_main.c:710 maps ERROR_NO_DATA to ERROR_READ_FAULT.
  case ERROR_READ_FAULT: return true;
  default:               return false;
  }
}
}
using TouchChannel = TouchProtocol::Channel;
namespace {
auto Touched(RdpeiServerContext const& context) -> TouchChannel& {
  return CallbackOwner<TouchChannel, &RdpeiServerContext::user_data>(context);
}
constexpr OperationName TouchEvent     { "Touch event"              };
constexpr OperationName TouchAssignment{ "Touch channel assignment" };
using sdl_rdp::freerdp_facade::AssignThrough;
using sdl_rdp::freerdp_facade::Handled;
}
template <> auto TouchProtocol::Open(PeerLink& link, Channel& channel) -> Context {
  auto context = Context{ rdpei_server_context_new(link.Channels()) };
  if (!context) return context;
  Install(*context, channel);
  return rdpei_server_init(context.get()) == CHANNEL_RC_OK ? std::move(context) : Context{ };
}
template <> auto TouchProtocol::Service(Context const& context) -> bool {
  return TouchHandled(rdpei_server_handle_messages(context.get()));
}
template <> auto TouchProtocol::Handle(Context const& context) -> WaitHandle {
  return rdpei_server_get_event_handle(context.get());
}
template <> auto TouchProtocol::Activate(Context const& context) -> bool {
  return rdpei_server_send_sc_ready(context.get(), RDPINPUT_PROTOCOL_V10, 0) == CHANNEL_RC_OK;
}
template <> auto TouchProtocol::Install(RdpeiServerContext& server, Channel& channel) -> void {
  server.user_data = &channel;
  constexpr auto failures = FailuresThrough<&TouchChannel::FailureSource>;
  constexpr auto touch    = [](TouchChannel& channel, RDPINPUT_TOUCH_EVENT const& event) {
    return channel._events.Touch(event);
  };
  constexpr auto assign   = AssignThrough<&TouchChannel::_slot>;
  // abi: rdpei onTouchEvent; onChannelIdAssigned, BOOL is int
  server.onTouchEvent        = Handled<Touched, touch, TouchEvent, failures, ERROR_INTERNAL_ERROR>;
  server.onChannelIdAssigned = Handled<Touched, assign, TouchAssignment, failures, false>;
}
}
