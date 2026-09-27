#include <sdl-rdp/peer/channel-set.hpp>

#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/video/display-control.hpp>

#include <cstdint>

namespace sdl_rdp::peer::detail::channel_set {
using sdl_rdp::utilities::Expects;

ChannelSet::ChannelSet(PeerLink& link, Activation const& activation, GraphicsLink& graphics, DisplayControl& display,
                       Redirection& redirection, Input& input, Diagnostics const& diagnostics)
    : LoggedFailures{ diagnostics }, _link{ link }, _activation{ activation }, _graphics{ graphics },
      _display{ display }, _redirection{ redirection }, _input{ input },
      _registration{ link.Channels().OnDynamicCreation(*this) } { }
auto ChannelSet::Pump(Signalled const& ready) -> bool {
  if (!_activation.Active()) return true;
  return _link.Channels().Pump() && _input.Channels(ready) && _redirection.OpenStatic(ready) && _display.Open()
         && _graphics.Pump(ready);
}
auto ChannelSet::Handles(std::span<WaitHandle> out) const -> std::span<WaitHandle> {
  Expects(out.size() >= ChannelHandleLimit, "handle span has room for every channel");
  return _graphics.Handles(_redirection.Handles(_input.Handles(out)));
}
auto ChannelSet::Created(std::uint32_t channel_id, std::int32_t status) -> bool {
  _link.Invalidate();
  if (status >= 0) return _link.Dynamic().Activate(channel_id);
  _link.Dynamic().Reject(channel_id);
  return true;
}
}
