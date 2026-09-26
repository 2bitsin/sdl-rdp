#include <sdl-rdp/video/graphics-link.hpp>

#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/diagnostics/log-level.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/video/acknowledgement-window.hpp>
#include <sdl-rdp/video/encoder.hpp>
#include <sdl-rdp/video/frame/pacing.hpp>

#include <freerdp/settings.h>
#include <algorithm>
#include <cstddef>
#include <utility>

namespace sdl_rdp::video::detail::graphics_link {
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::link::DynamicChannelsReady;
using sdl_rdp::utilities::Expects;

GraphicsLink::GraphicsLink(PeerLink& link, Diagnostics const& diagnostics, Activation& activation,
                           FramePacing const& pacing, Encoder const& encoder,
                           Factory<std::unique_ptr<GfxChannel>, DynamicChannel&> make) noexcept
    : _link{ link }, _diagnostics{ diagnostics }, _activation{ activation }, _pacing{ pacing }, _encoder{ encoder },
      _make{ std::move(make) } { }
auto GraphicsLink::Pump(Signalled const& ready) -> bool {
  if (_channel) return !ready.Contains(_channel->Event()) || _channel->Pump();
  if (_attempted || !freerdp_settings_get_bool(&_link.Settings(), FreeRDP_SupportGraphicsPipeline)
      || !DynamicChannelsReady(_link))
    return true;
  _attempted = true;
  _link.Invalidate();
  _channel = _make(*this);
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
auto GraphicsLink::Capacity() const -> std::size_t {
  return Confirmed() ? _channel->FrameWindow() : AcknowledgedFrameWindow;
}
auto GraphicsLink::Channel() const -> GfxChannel& {
  Expects(_channel != nullptr, "graphics channel exists");
  return *_channel;
}
auto GraphicsLink::Handles(std::span<WaitHandle> out) const -> std::span<WaitHandle> {
  Expects(out.size() >= GraphicsHandleLimit, "handle span has room for the graphics channel");
  if (!_channel) return out;
  out.front() = _channel->Event();
  return out.subspan(GraphicsHandleLimit);
}
auto GraphicsLink::Activate() -> bool {
  return true;
}
auto GraphicsLink::Reject() -> void {
  Expects(_channel != nullptr, "a rejected graphics channel is open");
  Abandon("GFX channel rejected; using legacy surface bits.");
}
auto GraphicsLink::Timing() const noexcept -> std::optional<std::reference_wrapper<GraphicsTiming const>> {
  if (!_channel) return std::nullopt;
  return std::cref(_channel->Timing());
}
auto GraphicsLink::Failures(OperationName operation) const noexcept -> FailureLog {
  return { _diagnostics, operation };
}
auto GraphicsLink::Abandon(std::string_view reason) -> void {
  _channel.reset();
  _diagnostics.Log(LogLevel::Warn, std::string{ reason });
  _activation.Announce(_encoder.SelectedCodec(), _pacing.Effective());
}
}
