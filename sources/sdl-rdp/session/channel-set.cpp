#include <sdl-rdp/session/channel-set.hpp>

#include <sdl-rdp/core/activation.hpp>
#include <sdl-rdp/core/peer-link.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/video/display-control.hpp>

#include <freerdp/channels/wtsvc.h>

namespace Backend {
namespace {
// abi: psDVCCreationStatusCallback
auto ChannelCreated(void* user, UINT32 channel_id, INT32 status) -> BOOL {
  return CallbackOwner<ChannelSet>(user).Created(channel_id, status);
}
auto Register(HANDLE manager, ChannelSet& channels) -> CreationRegistration {
  WTSVirtualChannelManagerSetDVCCreationCallback(manager, ChannelCreated, &channels);
  return CreationRegistration{ manager };
}
}
auto ForgetChannelCreation(HANDLE manager) -> void {
  WTSVirtualChannelManagerSetDVCCreationCallback(manager, nullptr, nullptr);
}
ChannelSet::ChannelSet(PeerLink& link, Activation const& activation, GraphicsLink& graphics, DisplayControl& display,
                       Redirection& redirection, Input& input)
    : _link{ link }, _activation{ activation }, _graphics{ graphics }, _display{ display }, _redirection{ redirection },
      _input{ input }, _registration{ Register(link.Channels(), *this) } { }
auto ChannelSet::Pump(std::span<HANDLE const> ready) -> bool {
  if (!_activation.Active()) return true;
  return WTSVirtualChannelManagerCheckFileDescriptor(_link.Channels()) && _input.Channels(ready)
         && _redirection.OpenStatic(ready) && _display.Open() && _graphics.Pump(ready);
}
auto ChannelSet::Handles(std::span<HANDLE> out) const -> std::span<HANDLE> {
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
