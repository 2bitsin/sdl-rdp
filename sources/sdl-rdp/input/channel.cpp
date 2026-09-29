#include <sdl-rdp/input/channel.hpp>

#include <sdl-rdp/input/events.hpp>
#include <sdl-rdp/link/peer-link.hpp>

namespace sdl_rdp::input::detail::channel {
template <class ChannelTy>
InputChannel<ChannelTy>::InputChannel(PeerLink& link, InputEvents& events, Diagnostics const& diagnostics)
    : _dynamic{ [this] { return Activate(); } }, _assignee{ link, diagnostics, _dynamic },
      _channel{ link.Channels(), events, _assignee } { }
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
