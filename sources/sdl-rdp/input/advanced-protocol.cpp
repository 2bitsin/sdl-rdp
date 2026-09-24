#include <sdl-rdp/input/advanced-protocol.hpp>

#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/input/input-events.hpp>

#include <freerdp/channels/wtsvc.h>
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
template <> auto AdvancedProtocol::Handle(Context const& context) -> HANDLE {
  HANDLE event = nullptr;
  return context->ChannelHandle(context.get(), &event) ? event : nullptr;
}
template <> auto AdvancedProtocol::Activate(Context const& context) -> bool {
  return Service(context);
}
template <> auto AdvancedProtocol::Install(Context const& context, Channel& channel) -> void {
  Expects(context != nullptr, "an installed input channel has its context");
  context->data              = &channel;
  // abi: psAInputServerMouseEvent
  context->MouseEvent        = [](ainput_server_context* owner, UINT64 /*timestamp*/, UINT64 flags, INT32 x,
                                  INT32 y) noexcept -> UINT {
    Expects(owner != nullptr, "callback context exists");
    return CallbackOwner<AdvancedChannel>(owner->data)._events.Pointer(flags, x, y);
  };
  // abi: psAInputChannelIdAssigned
  context->ChannelIdAssigned = [](ainput_server_context* owner, UINT32 id) noexcept -> BOOL {
    Expects(owner != nullptr, "callback context exists");
    CallbackOwner<AdvancedChannel>(owner->data)._slot.Assign(id);
    return true;
  };
}
}
