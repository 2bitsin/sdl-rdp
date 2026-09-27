#include <sdl-rdp/input/channel.hpp>

#include <sdl-rdp/freerdp-facade/assignment-sink.hpp>
#include <sdl-rdp/input/events.hpp>
#include <sdl-rdp/link/peer-link.hpp>

namespace sdl_rdp::input::detail::channel {
using sdl_rdp::freerdp_facade::AssignmentSink;

namespace {
template <class ChannelTy> auto Made(PeerLink& link, InputEvents& events, AssignmentSink& assignee) -> ChannelTy;
template <> auto Made<AdvancedInputChannel>(PeerLink& link, InputEvents& events, AssignmentSink& assignee)
    -> AdvancedInputChannel {
  return { link.Channels(), link.Connection(), events, assignee };
}
template <> auto Made<TouchChannel>(PeerLink& link, InputEvents& events, AssignmentSink& assignee) -> TouchChannel {
  return { link.Channels(), events, assignee };
}
}
template <class ChannelTy>
InputChannel<ChannelTy>::InputChannel(PeerLink& link, InputEvents& events, Diagnostics const& diagnostics) noexcept
    : _dynamic{ [this] { return Activate(); } }, _assignee{ link, diagnostics, _dynamic },
      _channel{ Made<ChannelTy>(link, events, _assignee) } { }
template <class ChannelTy> auto InputChannel<ChannelTy>::Open() -> bool {
  return _channel.Open();
}
template <class ChannelTy> auto InputChannel<ChannelTy>::Pump(Signalled const& ready) -> bool {
  return !ready.Contains(Event()) || _channel.Pump();
}
template <class ChannelTy> auto InputChannel<ChannelTy>::Event() const -> std::optional<WaitHandle> {
  if (!_ready) return std::nullopt;
  return _channel.Handle();
}
template <class ChannelTy> auto InputChannel<ChannelTy>::Activate() -> bool {
  _ready = true;
  return _channel.Activate();
}
template class InputChannel<AdvancedInputChannel>;
template class InputChannel<TouchChannel>;
}
