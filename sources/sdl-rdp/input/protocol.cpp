#include <sdl-rdp/input/protocol.hpp>

#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/input/events.hpp>
#include <sdl-rdp/utilities/contained.hpp>

#include <freerdp/channels/wtsvc.h>
#include <cstdint>
#include <utility>

namespace sdl_rdp::input::detail::protocol {
using sdl_rdp::diagnostics::FailuresThrough;
using sdl_rdp::freerdp_facade::CallbackOwner;
using sdl_rdp::utilities::OperationName;

namespace {
using AdvancedChannel = AdvancedProtocol::Channel;
auto Advanced(ainput_server_context const& context) -> AdvancedChannel& {
  return CallbackOwner<AdvancedChannel, &ainput_server_context::data>(context);
}
constexpr OperationName AdvancedMouse     { "Advanced input mouse event"        };
constexpr OperationName AdvancedAssignment{ "Advanced input channel assignment" };
using sdl_rdp::freerdp_facade::AssignThrough;
using sdl_rdp::freerdp_facade::Handled;
auto Started(AdvancedProtocol::Context const& context) -> bool {
  return context->Initialize(context.get(), true) == CHANNEL_RC_OK && context->Open(context.get()) == CHANNEL_RC_OK
         && AdvancedProtocol::Service(context) && AdvancedProtocol::Handle(context) != nullptr;
}
}
template <> auto AdvancedProtocol::Open(PeerLink& link, Channel& channel) -> Context {
  auto context = Context{ ainput_server_context_new(link.Channels()) };
  if (!context) return context;
  context->rdpcontext = &link.Context();
  Install(*context, channel);
  return Started(context) ? std::move(context) : Context{ };
}
template <> auto AdvancedProtocol::Service(Context const& context) -> bool {
  return context->Poll(context.get()) == CHANNEL_RC_OK;
}
template <> auto AdvancedProtocol::Handle(Context const& context) -> WaitHandle {
  WaitHandle event = nullptr;
  return context->ChannelHandle(context.get(), &event) ? event : nullptr;
}
template <> auto AdvancedProtocol::Activate(Context const& context) -> bool {
  return Service(context);
}
template <> auto AdvancedProtocol::Install(ainput_server_context& server, Channel& channel) -> void {
  server.data = &channel;
  constexpr auto failures = FailuresThrough<&AdvancedChannel::FailureSource>;
  constexpr auto pointer  = [](AdvancedChannel& channel, std::uint64_t /*timestamp*/, std::uint64_t flags,
                               std::int32_t x, std::int32_t y) { return channel._events.Pointer(flags, x, y); };
  constexpr auto assign   = AssignThrough<&AdvancedChannel::_slot>;
  // abi: psAInputServerMouseEvent; psAInputChannelIdAssigned, BOOL is int
  server.MouseEvent        = Handled<Advanced, pointer, AdvancedMouse, failures, ERROR_INTERNAL_ERROR>;
  server.ChannelIdAssigned = Handled<Advanced, assign, AdvancedAssignment, failures, false>;
}
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
