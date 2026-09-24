#include "_detail/graphics-link.hpp"

#include "_detail/acknowledgement-window.hpp"
#include "_detail/activation.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/encoder.hpp"
#include "_detail/frame-pacing.hpp"
#include "_detail/peer-link.hpp"

#include <algorithm>
#include <freerdp/settings.h>
#include <utility>

namespace Backend {
GraphicsLink::GraphicsLink(PeerLink& link, Diagnostics const& diagnostics, Activation& activation,
                           FramePacing const& pacing, Encoder const& encoder,
                           Factory<std::unique_ptr<GfxChannel>> make) noexcept
    : _link { link }, _diagnostics{ diagnostics }, _activation{ activation }, _pacing{ pacing }, _encoder{ encoder },
      _make{ std::move(make) } { }
bool GraphicsLink::Pump(std::span<HANDLE const> ready) {
  if (_channel) return !std::ranges::contains(ready, _channel->Event()) || _channel->Pump();
  if (_attempted || !freerdp_settings_get_bool(&_link.Settings(), FreeRDP_SupportGraphicsPipeline) ||
      !DynamicChannelsReady(_link))
    return true;
  _attempted = true;
  _link.Invalidate();
  _channel = _make();
  if (!_channel->Open()) Abandon("GFX channel open failed; using legacy surface bits.");
  return true;
}
void GraphicsLink::ExpireConfirmation() {
  if (!_activation.Holding() || Confirmed()) return;
  if (Activation::Clock::now() < _activation.ActivatedAt() + GraphicsConnectionWait) return;
  _link.Invalidate();
  _attempted = true;
  Abandon("GFX confirmation timed out; using legacy surface bits.");
}
bool GraphicsLink::Confirmed() const {
  return _channel && _channel->Confirmed();
}
unsigned GraphicsLink::Capacity() const {
  return Confirmed() ? _channel->FrameWindow() : AcknowledgedFrameWindow;
}
GfxChannel& GraphicsLink::Channel() const {
  Expects(_channel != nullptr, "graphics channel exists");
  return *_channel;
}
std::span<HANDLE> GraphicsLink::Handles(std::span<HANDLE> out) const {
  Expects(out.size() >= GraphicsHandleLimit, "handle span has room for the graphics channel");
  if (!_channel) return out;
  out.front() = _channel->Event();
  return out.subspan(GraphicsHandleLimit);
}
void GraphicsLink::Rejected(UINT32 channel_id) {
  if (_channel && _channel->Assigned(channel_id)) Abandon("GFX channel rejected; using legacy surface bits.");
}
GraphicsTiming const* GraphicsLink::Timing() const noexcept {
  return _channel ? &_channel->Timing() : nullptr;
}
void GraphicsLink::Abandon(char const* reason) {
  _channel.reset();
  _diagnostics.Log(SDLRDP_LOG_WARN, reason);
  _activation.Announce(_encoder.Codec(), _pacing.Effective());
}
}
