#include <sdl-rdp/input/advanced-protocol.hpp>

#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/input/input-events.hpp>
#include <sdl-rdp/utilities/contained.hpp>

#include <freerdp/channels/wtsvc.h>
#include <cstdint>
#include <utility>

namespace Backend {
namespace {
using AdvancedChannel = AdvancedProtocol::Channel;
auto Started(AdvancedProtocol::Context const& context) -> bool {
  return context->Initialize(context.get(), true) == CHANNEL_RC_OK && context->Open(context.get()) == CHANNEL_RC_OK
         && AdvancedProtocol::Service(context) && AdvancedProtocol::Handle(context) != nullptr;
}
}
template <> auto AdvancedProtocol::Open(PeerLink& link, Channel& channel) -> Context {
  auto context = Context{ ainput_server_context_new(link.Channels()) };
  if (!context) return context;
  context->rdpcontext = &link.Context();
  Install(context, channel);
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
template <> auto AdvancedProtocol::Install(Context const& context, Channel& channel) -> void {
  Expects(context != nullptr, "an installed input channel has its context");
  context->data = &channel;
  // abi: psAInputServerMouseEvent
  context->MouseEvent = [](ainput_server_context* owner, std::uint64_t /*timestamp*/, std::uint64_t flags,
                           std::int32_t x, std::int32_t y) noexcept -> std::uint32_t {
    Expects(owner != nullptr, "callback context exists");
    auto&      events  = CallbackOwner<AdvancedChannel>(owner->data)._events;
    auto const pointed = [&] { return events.Pointer(flags, x, y); };
    return Contained(ERROR_INTERNAL_ERROR, pointed, events.Failures("Advanced input mouse event"));
  };
  // abi: psAInputChannelIdAssigned, BOOL is int
  context->ChannelIdAssigned = [](ainput_server_context* owner, std::uint32_t id) noexcept -> int {
    Expects(owner != nullptr, "callback context exists");
    auto& channel = CallbackOwner<AdvancedChannel>(owner->data);
    return channel._slot.Assigned(id, channel._events.Failures("Advanced input channel assignment"));
  };
}
}
