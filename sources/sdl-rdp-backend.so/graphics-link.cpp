#include "_detail/graphics-link.hpp"

#include "_detail/acknowledgement-window.hpp"
#include "_detail/activation.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/encoder.hpp"
#include "_detail/frame-pacing.hpp"
#include "_detail/peer-link.hpp"

#include <freerdp/settings.h>
#include <algorithm>
#include <utility>

namespace Backend {
GraphicsLink::GraphicsLink(PeerLink& link, Diagnostics const& diagnostics, Activation& activation,
                           FramePacing const& pacing, Encoder const& encoder,
                           Factory<std::unique_ptr<GfxChannel>> make) noexcept
    : _link{ link }, _diagnostics{ diagnostics }, _activation{ activation }, _pacing{ pacing }, _encoder{ encoder },
      _make{ std::move(make) } { }
auto GraphicsLink::Pump(std::span<HANDLE const> ready) -> bool {
  if (_channel) return !std::ranges::contains(ready, _channel->Event()) || _channel->Pump();
  if (_attempted || !freerdp_settings_get_bool(&_link.Settings(), FreeRDP_SupportGraphicsPipeline)
      || !DynamicChannelsReady(_link))
    return true;
  _attempted = true;
  _link.Invalidate();
  _channel = _make();
  if (!_channel->Open()) Abandon("GFX channel open failed; using legacy surface bits.");
  return true;
}
auto GraphicsLink::ExpireConfirmation() -> void {
  if (!_activation.Holding() || Confirmed()) return;
  if (Activation::Clock::now() < _activation.ActivatedAt() + GraphicsConnectionWait) return;
  _link.Invalidate();
  _attempted = true;
  Abandon("GFX confirmation timed out; using legacy surface bits.");
}
auto GraphicsLink::Confirmed() const -> bool {
  return _channel && _channel->Confirmed();
}
auto GraphicsLink::Capacity() const -> unsigned {
  return Confirmed() ? _channel->FrameWindow() : AcknowledgedFrameWindow;
}
auto GraphicsLink::Channel() const -> GfxChannel& {
  Expects(_channel != nullptr, "graphics channel exists");
  return *_channel;
}
auto GraphicsLink::Handles(std::span<HANDLE> out) const -> std::span<HANDLE> {
  Expects(out.size() >= GraphicsHandleLimit, "handle span has room for the graphics channel");
  if (!_channel) return out;
  out.front() = _channel->Event();
  return out.subspan(GraphicsHandleLimit);
}
auto GraphicsLink::Rejected(UINT32 channel_id) -> void {
  if (_channel && _channel->Assigned(channel_id)) Abandon("GFX channel rejected; using legacy surface bits.");
}
auto GraphicsLink::Timing() const noexcept -> GraphicsTiming const* {
  return _channel ? &_channel->Timing() : nullptr;
}
auto GraphicsLink::Abandon(char const* reason) -> void {
  _channel.reset();
  _diagnostics.Log(SDLRDP_LOG_WARN, reason);
  _activation.Announce(_encoder.Codec(), _pacing.Effective());
}
}
