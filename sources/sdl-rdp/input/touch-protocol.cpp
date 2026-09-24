#include <sdl-rdp/input/touch-protocol.hpp>

#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/input/input-events.hpp>

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
template <> auto TouchProtocol::Open(PeerLink& link, Channel& channel) -> Context {
  auto context = Context{ rdpei_server_context_new(link.Channels()) };
  if (!context) return context;
  Install(context, channel);
  return rdpei_server_init(context.get()) == CHANNEL_RC_OK ? std::move(context) : Context{ };
}
template <> auto TouchProtocol::Service(Context const& context) -> bool {
  return TouchHandled(rdpei_server_handle_messages(context.get()));
}
template <> auto TouchProtocol::Handle(Context const& context) -> HANDLE {
  return rdpei_server_get_event_handle(context.get());
}
template <> auto TouchProtocol::Activate(Context const& context) -> bool {
  return rdpei_server_send_sc_ready(context.get(), RDPINPUT_PROTOCOL_V10, 0) == CHANNEL_RC_OK;
}
template <> auto TouchProtocol::Install(Context const& context, Channel& channel) -> void {
  Expects(context != nullptr, "an installed input channel has its context");
  context->user_data           = &channel;
  // abi: rdpei onTouchEvent
  context->onTouchEvent        = [](RdpeiServerContext* owner, RDPINPUT_TOUCH_EVENT const* event) noexcept -> UINT {
    Expects(owner != nullptr, "callback context exists");
    Expects(event != nullptr, "event is supplied");
    return CallbackOwner<TouchChannel>(owner->user_data)._events.Touch(*event);
  };
  // abi: rdpei onChannelIdAssigned
  context->onChannelIdAssigned = [](RdpeiServerContext* owner, UINT32 id) noexcept -> BOOL {
    Expects(owner != nullptr, "callback context exists");
    CallbackOwner<TouchChannel>(owner->user_data)._slot.Assign(id);
    return true;
  };
}
}
