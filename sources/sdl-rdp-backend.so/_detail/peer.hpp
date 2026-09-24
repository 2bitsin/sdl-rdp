#pragma once
#include "activation.hpp"
#include "activator.hpp"
#include "arrival.hpp"
#include "auth.hpp"
#include "capability-check.hpp"
#include "channel-set.hpp"
#include "departure.hpp"
#include "desktop-layout.hpp"
#include "display-control.hpp"
#include "encoder.hpp"
#include "frame-capture.hpp"
#include "frame-gate.hpp"
#include "frame-pacing.hpp"
#include "frame-sender.hpp"
#include "frame-statistics.hpp"
#include "graphics-link.hpp"
#include "input-events.hpp"
#include "input.hpp"
#include "legacy-frame.hpp"
#include "output-control.hpp"
#include "peer-callbacks.hpp"
#include "peer-frames.hpp"
#include "peer-link.hpp"
#include "peer-loop.hpp"
#include "peer-pump.hpp"
#include "peer-status.hpp"
#include "peer-wait.hpp"
#include "pinned.hpp"
#include "pointer-sender.hpp"
#include "redirection.hpp"
#include "scaler.hpp"
#include "trace-queue.hpp"
#include "transport-end.hpp"

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
