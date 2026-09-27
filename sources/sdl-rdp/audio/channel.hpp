#pragma once
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/diagnostics/logged-failures.hpp>
#include <sdl-rdp/freerdp-facade/sound-channel.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/link/forward.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace sdl_rdp::audio::detail::channel {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::diagnostics::LoggedFailures;
using sdl_rdp::diagnostics::TraceQueue;
using sdl_rdp::freerdp_facade::BlockConfirm;
using sdl_rdp::freerdp_facade::SoundChannel;
using sdl_rdp::freerdp_facade::SoundChannelEvents;
using sdl_rdp::freerdp_facade::SoundClient;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::link::EventQueue;
using sdl_rdp::link::PeerLink;
using sdl_rdp::link::SessionAccess;

inline constexpr std::uint32_t CompatibleRate = 44100;
inline constexpr std::uint32_t NativeRate     = 48000;
class AudioChannel final : public LoggedFailures<SoundChannelEvents> {
public:
  using Clock = std::chrono::steady_clock;
       AudioChannel(PeerLink& link, Diagnostics const& diagnostics, EventQueue& events, SessionAccess& session,
                    TraceQueue& traces);
  auto Initialize()                                -> bool;
  auto Pump()                                      -> bool;
  auto Event() const                               -> WaitHandle;
  auto Rate() const                                -> std::uint32_t;
  auto Remaining() const                           -> std::uint32_t;
  auto Reset()                                     -> void;
  auto LogAudio() const                            -> void;
  auto AdoptServerClock()                          -> void;
  auto Ready(std::uint32_t latency_ms)             -> bool;
  auto Send(std::span<std::int16_t const> samples) -> bool;
  auto Activated(SoundClient const& client)        -> void override;
  auto Confirmed(BlockConfirm confirm)             -> void override;

private:
  struct Block {
    std::uint8_t      id    { };
    std::uint64_t     frames{ };
    Clock::time_point sent;
  };
  auto SendBlock()                                            -> bool;
  auto RecordBlock(Clock::time_point now, std::uint8_t block) -> void;
  auto TransportEnded()                                       -> void;
  auto Credit()                                               -> std::uint64_t;
  auto ReportGate(bool available, std::uint64_t credit)       -> void;
  auto Select(std::size_t index)                              -> void;
  auto RejectFormats(SoundClient const& client)               -> void;
  PeerLink&                 _link;
  EventQueue&               _events;
  SessionAccess&            _session;
  TraceQueue&               _traces;
  SoundChannel              _sound;
  std::uint32_t             _rate            { };
  bool                      _rejected        { };
  bool                      _gate_warned     { };
  bool                      _ready           { };
  bool                      _server_clock    { };
  bool                      _has_confirmation{ };
  std::uint64_t             _sent            { };
  std::uint64_t             _confirmed       { };
  std::uint64_t             _clock_frames    { };
  Clock::time_point         _first;
  Clock::time_point         _clock_start;
  std::uint64_t             _blocks_sent     { };
  std::uint64_t             _gaps_over_40ms  { };
  Clock::time_point         _last_send;
  Clock::duration           _gap_total       { };
  Clock::duration           _gap_max         { };
  std::deque<Block>         _pending;
  std::vector<std::int16_t> _buffer;
};
}

namespace sdl_rdp::audio {
using detail::channel::AudioChannel;
}
