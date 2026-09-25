#include <sdl-rdp/peer/channel-set.hpp>

#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/video/display-control.hpp>

#include <freerdp/channels/wtsvc.h>
#include <cstdint>

namespace Backend {
class ChannelSet::Callbacks {
public:
  static auto Register(WaitHandle manager, ChannelSet& channels) -> CreationRegistration;

private:
  // abi: psDVCCreationStatusCallback, BOOL is int
  static auto Created(void* user, std::uint32_t channel_id, std::int32_t status) noexcept -> int;
};
auto ChannelSet::Callbacks::Register(WaitHandle manager, ChannelSet& channels) -> CreationRegistration {
  WTSVirtualChannelManagerSetDVCCreationCallback(manager, Created, &channels);
  return CreationRegistration{ manager };
}
auto ChannelSet::Callbacks::Created(void* user, std::uint32_t channel_id, std::int32_t status) noexcept -> int {
  auto&      owner   = CallbackOwner<ChannelSet>(user);
  auto const created = [&] { return owner.Created(channel_id, status); };
  return Contained(false, created, owner._graphics.Failures("Dynamic channel creation"));
}
auto ForgetChannelCreation(WaitHandle manager) -> void {
  WTSVirtualChannelManagerSetDVCCreationCallback(manager, nullptr, nullptr);
}
ChannelSet::ChannelSet(PeerLink& link, Activation const& activation, GraphicsLink& graphics, DisplayControl& display,
                       Redirection& redirection, Input& input)
    : _link{ link }, _activation{ activation }, _graphics{ graphics }, _display{ display }, _redirection{ redirection },
      _input{ input }, _registration{ Callbacks::Register(link.Channels(), *this) } { }
auto ChannelSet::Pump(std::span<WaitHandle const> ready) -> bool {
  if (!_activation.Active()) return true;
  return WTSVirtualChannelManagerCheckFileDescriptor(_link.Channels()) && _input.Channels(ready)
         && _redirection.OpenStatic(ready) && _display.Open() && _graphics.Pump(ready);
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
