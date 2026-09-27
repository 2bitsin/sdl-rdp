#pragma once
#include <sdl-rdp/audio/forward.hpp>
#include <sdl-rdp/auth/authenticator.hpp>
#include <sdl-rdp/clipboard/forward.hpp>
#include <sdl-rdp/configuration/forward.hpp>
#include <sdl-rdp/diagnostics/logged-failures.hpp>
#include <sdl-rdp/diagnostics/trace-queue.hpp>
#include <sdl-rdp/freerdp-facade/connection-events.hpp>
#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/freerdp-facade/signalled.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/input/events.hpp>
#include <sdl-rdp/input/input.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/peer/arrival.hpp>
#include <sdl-rdp/peer/channel-set.hpp>
#include <sdl-rdp/peer/redirection.hpp>
#include <sdl-rdp/peer/status.hpp>
#include <sdl-rdp/peer/transport-end.hpp>
#include <sdl-rdp/picture/desktop-layout.hpp>
#include <sdl-rdp/utilities/generational.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/nt-owf.hpp>
#include <sdl-rdp/video/display-control.hpp>
#include <sdl-rdp/video/encoder.hpp>
#include <sdl-rdp/video/frame/capture.hpp>
#include <sdl-rdp/video/frame/gate.hpp>
#include <sdl-rdp/video/frame/pacing.hpp>
#include <sdl-rdp/video/frame/sender.hpp>
#include <sdl-rdp/video/frame/statistics.hpp>
#include <sdl-rdp/video/graphics-link.hpp>
#include <sdl-rdp/video/legacy-frame.hpp>
#include <sdl-rdp/video/output-control.hpp>
#include <sdl-rdp/video/peer-frames.hpp>
#include <sdl-rdp/video/pointer/forward.hpp>
#include <sdl-rdp/video/pointer/sender.hpp>
#include <sdl-rdp/video/pointer/shape.hpp>
#include <sdl-rdp/video/scaler.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string_view>
#include <thread>

namespace sdl_rdp::peer::detail::peer {
using sdl_rdp::audio::AudioChannel;
using sdl_rdp::auth::Authenticator;
using sdl_rdp::clipboard::ClipboardStore;
using sdl_rdp::configuration::Configuration;
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::diagnostics::LoggedFailures;
using sdl_rdp::diagnostics::TraceQueue;
using sdl_rdp::freerdp_facade::Connection;
using sdl_rdp::freerdp_facade::ConnectionEvents;
using sdl_rdp::freerdp_facade::Identity;
using sdl_rdp::freerdp_facade::Observation;
using sdl_rdp::freerdp_facade::Signalled;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::input::Input;
using sdl_rdp::input::InputEvents;
using sdl_rdp::input::MouseMode;
using sdl_rdp::link::Activation;
using sdl_rdp::link::EventQueue;
using sdl_rdp::link::PeerLink;
using sdl_rdp::link::SessionAccess;
using sdl_rdp::picture::DesktopLayout;
using sdl_rdp::picture::FrameLock;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::utilities::Generational;
using sdl_rdp::utilities::NtOwf;
using sdl_rdp::utilities::Rect;
using sdl_rdp::video::DisplayControl;
using sdl_rdp::video::Encoder;
using sdl_rdp::video::GraphicsLink;
using sdl_rdp::video::LegacyFrame;
using sdl_rdp::video::OutputControl;
using sdl_rdp::video::PeerFrames;
using sdl_rdp::video::Scaler;
using sdl_rdp::video::frame::FrameCapture;
using sdl_rdp::video::frame::FrameGate;
using sdl_rdp::video::frame::FramePacing;
using sdl_rdp::video::frame::FrameSender;
using sdl_rdp::video::frame::FrameStatistics;
using sdl_rdp::video::pointer::PointerSender;
using sdl_rdp::video::pointer::PointerShape;

class Peer final : public LoggedFailures<ConnectionEvents> {
public:
       Peer(Connection accepted, Diagnostics const& diagnostics, EventQueue& events, Configuration const& configuration,
            FrameStore& store, Generational<PointerShape>& pointer, ClipboardStore& clipboard, SessionAccess& session);
       ~Peer() override;
  auto Start()                                                      -> void;
  auto Stop()                                                       -> void;
  auto Owns(PeerLink const& link) const noexcept                    -> bool;
  auto Evict()                                                      -> bool;
  auto Finished() const noexcept                                    -> bool;
  auto Present(FrameLock const& held, std::span<Rect const> damage) -> void;
  auto Repaint(FrameLock const& held, Rect area)                    -> void;
  auto RestartPacing(FrameLock const& held)                         -> void;
  auto Signal()                                                     -> void;
  auto Settled(FrameLock const& held, std::uint64_t target) const   -> bool;
  auto Redirected() const noexcept                                  -> Redirection const&;
  auto Point(MouseMode mode)                                        -> void;
  auto Status(FrameLock const& held)                                -> PeerStatus;

private:
  struct WaitPlan {
    std::uint32_t count  { };
    std::uint32_t timeout{ };
  };

  auto Serve(std::stop_token const& quit)                                                          -> void;
  auto Run(std::stop_token const& quit)                                                            -> void;
  auto Configure()                                                                                 -> void;
  auto Step(std::stop_token const& quit, std::span<WaitHandle> handles)                            -> bool;
  auto Dispatch(std::stop_token const& quit, std::span<WaitHandle> handles, std::uint32_t timeout) -> bool;
  auto Plan(std::span<WaitHandle> handles)                                                         -> WaitPlan;
  auto CollectHandles(std::span<WaitHandle> handles)                                               -> std::uint32_t;
  auto WaitTimeout()                                                                               -> std::uint32_t;
  auto Service(std::stop_token const& quit, Signalled const& ready)                                -> bool;
  auto Exchange(std::stop_token const& quit, Signalled const& ready)                               -> bool;
  auto Deliver(std::stop_token const& quit)                                                        -> bool;
  auto Ended()                                                                                     -> bool;

  auto Depart()             -> void;
  auto LogDeparture() const -> void;

  auto Activate()                             -> bool                 override;
  auto Capabilities()                         -> bool                 override;
  auto Logon(bool automatic)                  -> bool                 override;
  auto NtlmHash(Identity const& identity)     -> std::optional<NtOwf> override;
  auto NtlmRefused(std::string_view cause)    -> void                 override;
  auto FrameAcknowledged(std::uint32_t frame) -> void                 override;
  auto SuppressOutput(bool allow)             -> void                 override;

  Configuration const& _configuration;
  FrameStore&          _store;
  SessionAccess&       _session;

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
  OutputControl   _output;
  Observation     _observation;
  std::jthread    _thread;
};
}

namespace sdl_rdp::peer {
using detail::peer::Peer;
}
