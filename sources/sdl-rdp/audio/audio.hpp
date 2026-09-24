#pragma once
#include <sdl-rdp/freerdp-facade/releases-sound.hpp>

#include <freerdp/server/rdpsnd.h>
#include <chrono>
#include <deque>
#include <span>
#include <vector>

namespace Backend {
inline constexpr UINT32 CompatibleRate = 44100;
inline constexpr UINT32 NativeRate     = 48000;
class Diagnostics;
class EventQueue;
class PeerLink;
class SessionAccess;
class TraceQueue;
class AudioChannel {
public:
       AudioChannel(AudioChannel const&)                       = delete;
       AudioChannel(AudioChannel&&)                            = delete;
  using Clock = std::chrono::steady_clock;
       AudioChannel(PeerLink& link, Diagnostics const& diagnostics, EventQueue& events, SessionAccess& session,
                    TraceQueue& traces);
       ~AudioChannel();
  auto operator=(AudioChannel const&)         -> AudioChannel& = delete;
  auto operator=(AudioChannel&&)              -> AudioChannel& = delete;
  auto Initialize()                           -> bool;
  auto Pump()                                 -> bool;
  auto Event() const                          -> HANDLE;
  auto Rate() const                           -> unsigned;
  auto Remaining() const                      -> unsigned;
  auto Reset()                                -> void;
  auto LogAudio() const                       -> void;
  auto AdoptServerClock()                     -> void;
  auto Ready(unsigned latency_ms)             -> bool;
  auto Send(std::span<int16_t const> samples) -> bool;

private:
  struct Block {
    BYTE              id    { };
    uint64_t          frames{ };
    Clock::time_point sent;
  };
  auto        SendBlock()                                                        -> bool;
  auto        RecordBlock(Clock::time_point now, BYTE block)                     -> void;
  auto        TransportEnded()                                                   -> void;
  auto        Credit()                                                           -> uint64_t;
  auto        ReportGate(bool available, uint64_t credit)                        -> void;
  auto        Select(unsigned index)                                             -> void;
  auto        RejectFormats()                                                    -> void;
  auto        Confirm(BYTE id, UINT16 timestamp)                                 -> void;
  static auto Activated(RdpsndServerContext* context)                            -> void;
  static auto Confirmed(RdpsndServerContext* context, BYTE id, UINT16 timestamp) -> UINT;
  PeerLink&            _link;
  Diagnostics const&   _diagnostics;
  EventQueue&          _events;
  SessionAccess&       _session;
  TraceQueue&          _traces;
  SoundContext         _sound;
  AUDIO_FORMAT         _selected        { };
  bool                 _rejected        { };
  bool                 _gate_warned     { };
  bool                 _ready           { };
  bool                 _server_clock    { };
  bool                 _has_confirmation{ };
  uint64_t             _sent            { };
  uint64_t             _confirmed       { };
  uint64_t             _clock_frames    { };
  Clock::time_point    _first;
  Clock::time_point    _clock_start;
  uint64_t             _blocks_sent     { };
  uint64_t             _gaps_over_40ms  { };
  Clock::time_point    _last_send;
  Clock::duration      _gap_total       { };
  Clock::duration      _gap_max         { };
  std::deque<Block>    _pending;
  std::vector<int16_t> _buffer;
};
}
