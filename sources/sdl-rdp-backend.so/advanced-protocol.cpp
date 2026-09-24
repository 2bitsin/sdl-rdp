#include "_detail/advanced-protocol.hpp"

#include "_detail/callback-owner.hpp"
#include "_detail/input-events.hpp"

#include <freerdp/channels/wtsvc.h>
#include <utility>

namespace Backend {
using AdvancedChannel = InputChannel<AdvancedProtocol>;
auto AdvancedProtocol::Open(PeerLink& link, AdvancedChannel& channel) -> Context {
  auto context = Context{ ainput_server_context_new(link.Channels()) };
  if (!context) return context;
  Install(context, link, channel);
  return Started(context) ? std::move(context) : Context{ };
}
auto AdvancedProtocol::Service(Context const& context) -> bool {
  return context->Poll(context.get()) == CHANNEL_RC_OK;
}
auto AdvancedProtocol::Handle(Context const& context) -> HANDLE {
  HANDLE event = nullptr;
  return context->ChannelHandle(context.get(), &event) ? event : nullptr;
}
auto AdvancedProtocol::Activate(Context const& context) -> bool {
  return Service(context);
}
auto AdvancedProtocol::Install(Context const& context, PeerLink& link, AdvancedChannel& channel) -> void {
  Expects(context != nullptr, "an installed input channel has its context");
  context->data              = &channel;
  context->rdpcontext        = &link.Context();
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
auto AdvancedProtocol::Started(Context const& context) -> bool {
  return context->Initialize(context.get(), true) == CHANNEL_RC_OK && context->Open(context.get()) == CHANNEL_RC_OK
         && Service(context) && Handle(context) != nullptr;
}
}
