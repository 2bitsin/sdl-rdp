#pragma once
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/freerdp-facade/advanced-input-channel.hpp>
#include <sdl-rdp/freerdp-facade/signalled.hpp>
#include <sdl-rdp/freerdp-facade/touch-channel.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/input/activated-channel.hpp>
#include <sdl-rdp/input/channel-assignment.hpp>
#include <sdl-rdp/input/forward.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <optional>

namespace sdl_rdp::input::detail::channel {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::freerdp_facade::AdvancedInputChannel;
using sdl_rdp::freerdp_facade::Signalled;
using sdl_rdp::freerdp_facade::TouchChannel;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::link::PeerLink;
using sdl_rdp::utilities::Pinned;

// A dynamic input channel of the facade, opened once the dynamic channels are ready and serviced once activated.
template <class ChannelTy> class InputChannel final : private Pinned {
public:
       InputChannel(PeerLink& link, InputEvents& events, Diagnostics const& diagnostics) noexcept;
  auto Open()                       -> bool;
  auto Pump(Signalled const& ready) -> bool;
  auto Event() const                -> std::optional<WaitHandle>;

private:
  auto Activate() -> bool;
  // Each member refers to the one above it; destruction runs in reverse and makes no project callback.
  ActivatedChannel  _dynamic;
  ChannelAssignment _assignee;
  ChannelTy         _channel;
  bool              _ready   { };
};
extern template class InputChannel<AdvancedInputChannel>;
extern template class InputChannel<TouchChannel>;
}

namespace sdl_rdp::input {
using detail::channel::InputChannel;
}
