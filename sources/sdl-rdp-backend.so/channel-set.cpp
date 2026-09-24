#include "_detail/channel-set.hpp"

#include "_detail/activation.hpp"
#include "_detail/callback-owner.hpp"
#include "_detail/display-control.hpp"
#include "_detail/peer-link.hpp"

#include <freerdp/channels/wtsvc.h>

namespace Backend {
namespace {
BOOL ChannelCreated(void* user, UINT32 channel_id, INT32 status) {
  return CallbackOwner<ChannelSet>(user).Created(channel_id, status);
}
CreationRegistration Register(HANDLE manager, ChannelSet& channels) {
  WTSVirtualChannelManagerSetDVCCreationCallback(manager, ChannelCreated, &channels);
  return CreationRegistration{ manager };
}
}
void ForgetChannelCreation(HANDLE manager) {
  WTSVirtualChannelManagerSetDVCCreationCallback(manager, nullptr, nullptr);
}
ChannelSet::ChannelSet(PeerLink& link, Activation const& activation, GraphicsLink& graphics, DisplayControl& display,
                       Redirection& redirection, Input& input)
    : _link { link }, _activation{ activation }, _graphics{ graphics }, _display{ display },
      _redirection{ redirection }, _input{ input }, _registration{ Register(link.Channels(), *this) } { }
bool ChannelSet::Pump(std::span<HANDLE const> ready) {
  if (!_activation.Active()) return true;
  return WTSVirtualChannelManagerCheckFileDescriptor(_link.Channels()) && _input.Channels(ready) &&
         _redirection.OpenStatic(ready) && _display.Open() && _graphics.Pump(ready);
}
std::span<HANDLE> ChannelSet::Handles(std::span<HANDLE> out) const {
  Expects(out.size() >= ChannelHandleLimit, "handle span has room for every channel");
  return _graphics.Handles(_redirection.Handles(_input.Handles(out)));
}
BOOL ChannelSet::Created(UINT32 channel_id, INT32 status) {
  _link.Invalidate();
  if (status < 0) {
    _graphics.Rejected(channel_id);
    return TRUE;
  }
  if (auto activated = _input.Activate(channel_id)) return *activated;
  return _display.Activate(channel_id).value_or(TRUE);
}
}
