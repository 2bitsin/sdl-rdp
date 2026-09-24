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
  void                          Start();
  void                          Stop();
  bool                          Owns(PeerLink const& link) const noexcept;
  bool                          Evict();
  bool                          Finished() const                 noexcept;
  void                          Present(FrameLock const& held, std::span<sdlrdp_rect const> damage);
  void                          Repaint(FrameLock const& held, sdlrdp_rect area);
  void                          RestartPacing(FrameLock const& held);
  void                          Signal();
  bool                          Settled(FrameLock const& held, uint64_t target) const;
  AudioChannel*                 Audio() const                    noexcept;
  std::shared_ptr<DriveChannel> Drive() const;
  void                          Point(MouseMode mode);
  PeerStatus                    Status(FrameLock const& held) const;

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
