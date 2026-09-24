#include "_detail/peer.hpp"

#include "_detail/audio.hpp"
#include "_detail/clipboard.hpp"
#include "_detail/drive-channel.hpp"
#include "_detail/gfx.hpp"

#include <algorithm>
#include <freerdp/error.h>
#include <utility>

namespace Backend {
Peer::Peer(PeerHandle accepted, Diagnostics const& diagnostics, EventQueue& events, Configuration const& configuration,
           FrameStore& store, PointerStore& pointer, ClipboardStore& clipboard, SessionAccess& session)
    : _link { std::move(accepted) }, _traces{ diagnostics }, _activation{ events, _link }, _frames{ store },
      _pacing{ diagnostics, events, configuration, store, _link, _activation, _traces, _statistics },
      _scaler{ _frames, _desktop }, _authenticator{ _link, configuration, diagnostics },
      _graphics{ _link, diagnostics, _activation, _pacing, _encoder,
                 [&, this] {
                   return std::make_unique<GfxChannel>(_link, diagnostics, configuration, _activation, _frames,
                                                       _pacing, _encoder, _scaler);
                 } },
      _display     { _link, _activation, _desktop, events                              },
      _input_events{ _link, _activation, _desktop, events, store, diagnostics, session },
      _input       { _link, _input_events                                              },
      _redirection{ _link, _activation, session,
                    [&, this] { return std::make_unique<AudioChannel>(_link, diagnostics, events, session, _traces); },
                    [&, this] {
                      return std::make_unique<ClipboardChannel>(_link, _activation, clipboard, events, diagnostics);
                    },
                    [&, this] { return std::make_shared<DriveChannel>(_link, events, diagnostics, session); } },
      _channels    { _link, _activation, _graphics, _display, _redirection, _input               },
      _legacy      { _link, configuration, _activation, _frames, _pacing, _encoder, _scaler      },
      _pointer     { pointer, _link, diagnostics                                                 },
      _gate        { _link, store, _frames, _desktop, _pacing, _activation, _graphics            },
      _capture     { _link, store, _frames, _desktop, _pacing, _statistics, _encoder             },
      _sender      { _link, _activation, session, _gate, _capture, _pointer, _graphics, _legacy  },
      _end         { _link, _activation, _authenticator, _frames, store, diagnostics             },
      _arrival     { session, store, _frames, _link, _activation, _desktop, _pacing, diagnostics },
      _activator   { _link, _authenticator, _activation, _encoder, configuration, _arrival       },
      _capabilities{ _link, _authenticator, _activation, _pacing, _desktop, store, diagnostics   },
      _output      { _link, _graphics, _pacing, _activation, _frames                             },
      _callbacks   { _link, _authenticator, _activator, _capabilities, _output, _input_events    },
      _wait        { _link, _channels, _activation, _pacing, _graphics                           },
      _pump        { _link, session, _channels, _redirection, _sender, _end, _traces             },
      _departure   { _link, session, _activation, _redirection, _statistics, diagnostics         },
      _loop        { _link, session, diagnostics, configuration, store, _wait, _pump, _departure } { }
void Peer::Start() {
  _loop.Start();
}
void Peer::Stop() {
  _loop.Stop();
}
bool Peer::Owns(PeerLink const& link) const noexcept {
  return &_link == &link;
}
bool Peer::Evict() {
  if (!_activation.Deactivate()) return false;
  _redirection.Disconnect();
  _link.Refuse(ERRINFO_DISCONNECTED_BY_OTHER_CONNECTION);
  _link.Close();
  Stop();
  return true;
}
bool Peer::Finished() const noexcept {
  return _activation.Finished();
}
void Peer::Present(FrameLock const& held, std::span<sdlrdp_rect const> damage) {
  if (!_activation.Active()) return;
  _frames.CountPresent(held);
  std::ranges::for_each(damage, [&](sdlrdp_rect area) { _frames.Post(held, area); });
  _link.Signal();
}
void Peer::Repaint(FrameLock const& held, sdlrdp_rect area) {
  if (!_activation.Active()) return;
  _frames.Repaint(held, area);
  _link.Signal();
}
void Peer::RestartPacing(FrameLock const& held) {
  _pacing.Restart(held);
}
void Peer::Signal() {
  if (_activation.Active()) _link.Signal();
}
bool Peer::Settled(FrameLock const& held, uint64_t target) const {
  return _pacing.Settled(held, target);
}
AudioChannel* Peer::Audio() const noexcept {
  return _redirection.Audio();
}
std::shared_ptr<DriveChannel> Peer::Drive() const {
  return _redirection.Drive();
}
void Peer::Point(MouseMode mode) {
  _input_events.Point(mode);
  _link.Signal();
}
PeerStatus Peer::Status(FrameLock const& held) const {
  auto const* timing = _graphics.Timing();
  return { .client           = &_link.Client(),
           .display          = _display.Opened(),
           .desktop          = _desktop.Rect(),
           .resizing         = _desktop.Resizing(),
           .holding          = _activation.Holding(),
           .activated_at     = _activation.ActivatedAt(),
           .graphics         = timing ? std::optional{ *timing } : std::nullopt,
           .frame            = _pacing.Frame(),
           .acknowledged     = _pacing.Acknowledged(held),
           .acknowledgements = _statistics.Acknowledgements(),
           .encode_time      = _encoder.EncodeTime() };
}
}
