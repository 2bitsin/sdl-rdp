#pragma once
#include <sdl-rdp/auth/auth.hpp>
#include <sdl-rdp/core/activation.hpp>
#include <sdl-rdp/core/desktop-layout.hpp>
#include <sdl-rdp/core/peer-link.hpp>
#include <sdl-rdp/core/trace-queue.hpp>
#include <sdl-rdp/input/input-events.hpp>
#include <sdl-rdp/input/input.hpp>
#include <sdl-rdp/session/activator.hpp>
#include <sdl-rdp/session/arrival.hpp>
#include <sdl-rdp/session/capability-check.hpp>
#include <sdl-rdp/session/channel-set.hpp>
#include <sdl-rdp/session/departure.hpp>
#include <sdl-rdp/session/peer-callbacks.hpp>
#include <sdl-rdp/session/peer-loop.hpp>
#include <sdl-rdp/session/peer-pump.hpp>
#include <sdl-rdp/session/peer-status.hpp>
#include <sdl-rdp/session/peer-wait.hpp>
#include <sdl-rdp/session/redirection.hpp>
#include <sdl-rdp/session/transport-end.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/display-control.hpp>
#include <sdl-rdp/video/encoder.hpp>
#include <sdl-rdp/video/frame-capture.hpp>
#include <sdl-rdp/video/frame-gate.hpp>
#include <sdl-rdp/video/frame-pacing.hpp>
#include <sdl-rdp/video/frame-sender.hpp>
#include <sdl-rdp/video/frame-statistics.hpp>
#include <sdl-rdp/video/graphics-link.hpp>
#include <sdl-rdp/video/legacy-frame.hpp>
#include <sdl-rdp/video/output-control.hpp>
#include <sdl-rdp/video/peer-frames.hpp>
#include <sdl-rdp/video/pointer-sender.hpp>
#include <sdl-rdp/video/scaler.hpp>

#include <memory>
#include <span>

namespace Backend {
class AudioChannel;
class ClipboardStore;
class Configuration;
class Diagnostics;
class DriveChannel;
class EventQueue;
class PointerStore;
class SessionAccess;
class Peer : private Pinned {
public:
       Peer(PeerHandle accepted, Diagnostics const& diagnostics, EventQueue& events, Configuration const& configuration,
            FrameStore& store, PointerStore& pointer, ClipboardStore& clipboard, SessionAccess& session);
  auto Start()                                                             -> void;
  auto Stop()                                                              -> void;
  auto Owns(PeerLink const& link) const noexcept                           -> bool;
  auto Evict()                                                             -> bool;
  auto Finished() const noexcept                                           -> bool;
  auto Present(FrameLock const& held, std::span<sdlrdp_rect const> damage) -> void;
  auto Repaint(FrameLock const& held, sdlrdp_rect area)                    -> void;
  auto RestartPacing(FrameLock const& held)                                -> void;
  auto Signal()                                                            -> void;
  auto Settled(FrameLock const& held, uint64_t target) const               -> bool;
  auto Audio() const noexcept                                              -> AudioChannel*;
  auto Drive() const                                                       -> std::shared_ptr<DriveChannel>;
  auto Point(MouseMode mode)                                               -> void;
  auto Status(FrameLock const& held) const                                 -> PeerStatus;

private:
  PeerLink        _link;
  TraceQueue      _traces;
  Activation      _activation;
  PeerFrames      _frames;
  DesktopLayout   _desktop;
  FrameStatistics _statistics;
  FramePacing     _pacing;
  Encoder         _encoder;
  Scaler          _scaler;
  Authenticator   _authenticator;
  GraphicsLink    _graphics;
  DisplayControl  _display;
  InputEvents     _input_events;
  Input           _input;
  Redirection     _redirection;
  ChannelSet      _channels;
  LegacyFrame     _legacy;
  PointerSender   _pointer;
  FrameGate       _gate;
  FrameCapture    _capture;
  FrameSender     _sender;
  TransportEnd    _end;
  Arrival         _arrival;
  Activator       _activator;
  CapabilityCheck _capabilities;
  OutputControl   _output;
  PeerCallbacks   _callbacks;
  PeerWait        _wait;
  PeerPump        _pump;
  Departure       _departure;
  PeerLoop        _loop;
};
}
