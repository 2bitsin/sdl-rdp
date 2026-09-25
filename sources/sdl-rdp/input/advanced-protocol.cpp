#include <sdl-rdp/input/advanced-protocol.hpp>

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
}
