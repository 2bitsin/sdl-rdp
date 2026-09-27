#include <sdl-rdp/peer/peer.hpp>

#include <sdl-rdp/audio/channel.hpp>
#include <sdl-rdp/clipboard/channel.hpp>
#include <sdl-rdp/drive/channel.hpp>
#include <sdl-rdp/video/gfx/channel.hpp>

#include <algorithm>
#include <utility>

namespace sdl_rdp::peer::detail::peer {
using sdl_rdp::clipboard::ClipboardChannel;
using sdl_rdp::drive::DriveChannel;
using sdl_rdp::freerdp_facade::Refusal;
using sdl_rdp::link::DynamicChannel;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Rect;
using sdl_rdp::video::gfx::FrameSources;
using sdl_rdp::video::gfx::GfxChannel;
Peer::Peer(Connection accepted, Diagnostics const& diagnostics, EventQueue& events, Configuration const& configuration,
           FrameStore& store, Generational<PointerShape>& pointer, ClipboardStore& clipboard, SessionAccess& session)
    : _diagnostics{ diagnostics }, _configuration{ configuration }, _store{ store }, _session{ session },
      _link{ std::move(accepted) }, _traces{ diagnostics }, _activation{ events, _link }, _frames{ store },
      _pacing{ diagnostics, events, configuration, store, _link, _activation, _traces, _statistics },
      _scaler{ _frames, _desktop }, _authenticator{ _link, configuration, diagnostics },
      _graphics{ _link,
                 diagnostics,
                 _activation,
                 _pacing,
                 _encoder,
                 [&, this](DynamicChannel& owner) {
                   auto const sources = FrameSources{
                     .frames = _frames, .pacing = _pacing, .encoder = _encoder, .scaler = _scaler
                   };
                   return std::make_unique<GfxChannel>(_link, diagnostics, configuration, _activation, sources, owner);
                 } },
      _display     { _link, _activation, _desktop, events, diagnostics                 },
      _input_events{ _link, _activation, _desktop, events, store, diagnostics, session },
      _input       { _link, _input_events                                              },
      _redirection{ _link,
                    _activation,
                    session,
                    [&, this] { return std::make_unique<AudioChannel>(_link, diagnostics, events, session, _traces); },
                    [&, this] {
                      return std::make_unique<ClipboardChannel>(_link, _activation, clipboard, events, diagnostics);
                    },
                    [&, this] { return std::make_shared<DriveChannel>(_link, events, diagnostics, session); } },
      _channels   { _link, _activation, _graphics, _display, _redirection, _input               },
      _legacy     { _link, configuration, _activation, _frames, _pacing, _encoder, _scaler      },
      _pointer    { pointer, _link, diagnostics                                                 },
      _gate       { _link, store, _frames, _desktop, _pacing, _activation, _graphics            },
      _capture    { _link, store, _frames, _desktop, _pacing, _statistics, _encoder             },
      _sender     { _link, _activation, session, _gate, _capture, _pointer, _graphics, _legacy  },
      _end        { _link, _activation, _authenticator, _frames, store, diagnostics             },
      _arrival    { session, store, _frames, _link, _activation, _desktop, _pacing, diagnostics },
      _output     { _link, _graphics, _pacing, _activation, _frames                             },
      _observation{ _link.Connection().Observe(*this, _input_events)                            } { }
Peer::~Peer() {
  // The loop thread ends before its callbacks lose their owner.
  Stop();
  if (_thread.joinable()) _thread.join();
}
auto Peer::Start() -> void {
  Expects(!_thread.joinable(), "peer starts once");
  _thread = std::jthread([this](std::stop_token const& quit) { Serve(quit); });
}
auto Peer::Stop() -> void {
  _thread.request_stop();
}
auto Peer::Owns(PeerLink const& link) const noexcept -> bool {
  return &_link == &link;
}
auto Peer::Evict() -> bool {
  if (!_activation.Deactivate()) return false;
  _redirection.Disconnect();
  auto& connection = _link.Connection();
  connection.Refuse(Refusal::OtherConnection);
  connection.Close();
  Stop();
  return true;
}
auto Peer::Finished() const noexcept -> bool {
  return _activation.Finished();
}
auto Peer::Present(FrameLock const& held, std::span<Rect const> damage) -> void {
  if (!_activation.Active()) return;
  _frames.CountPresent(held);
  std::ranges::for_each(damage, [&](Rect area) { _frames.Post(held, area); });
  _link.Signal();
}
auto Peer::Repaint(FrameLock const& held, Rect area) -> void {
  if (!_activation.Active()) return;
  _frames.Repaint(held, area);
  _link.Signal();
}
auto Peer::RestartPacing(FrameLock const& held) -> void {
  _pacing.Restart(held);
}
auto Peer::Signal() -> void {
  if (_activation.Active()) _link.Signal();
}
auto Peer::Settled(FrameLock const& held, std::uint64_t target) const -> bool {
  return _pacing.Settled(held, target);
}
auto Peer::Redirected() const noexcept -> Redirection const& {
  return _redirection;
}
auto Peer::Point(MouseMode mode) -> void {
  _input_events.Point(mode);
  _link.Signal();
}
auto Peer::Status(FrameLock const& held) -> PeerStatus {
  auto const timing = _graphics.Timing();
  return { .connection       = std::ref(_link.Connection()),
           .display          = _display.Opened(),
           .desktop          = _desktop.Desktop(),
           .resizing         = _desktop.Resizing(),
           .holding          = _activation.Holding(),
           .activated_at     = _activation.ActivatedAt(),
           .graphics         = timing ? std::optional{ timing->get() } : std::nullopt,
           .frame            = _pacing.Frame(),
           .acknowledged     = _pacing.Acknowledged(held),
           .acknowledgements = _statistics.Acknowledgements(),
           .encode_time      = _encoder.EncodeTime() };
}
}
