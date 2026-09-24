#pragma once
#include "rdp-handles.hpp"

#include <chrono>
#include <deque>
#include <freerdp/server/rdpsnd.h>
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
                AudioChannel(AudioChannel const&) = delete;
                AudioChannel(AudioChannel&&)      = delete;
  using Clock = std::chrono::steady_clock;
                AudioChannel(PeerLink& link, Diagnostics const& diagnostics, EventQueue& events, SessionAccess& session,
                             TraceQueue& traces);
                ~AudioChannel();
  AudioChannel& operator = (AudioChannel const&)  = delete;
  AudioChannel& operator = (AudioChannel&&)       = delete;
  bool          Initialize();
  bool          Pump();
  HANDLE        Event() const;
  unsigned      Rate() const;
  unsigned      Remaining() const;
  void          Reset();
  void          LogAudio() const;
  void          AdoptServerClock();
  bool          Ready(unsigned latency_ms);
  bool          Send(std::span<int16_t const> samples);

private:
  struct Block {
    BYTE              id    { };
    uint64_t          frames{ };
    Clock::time_point sent;
  };
  bool        SendBlock();
  void        RecordBlock(Clock::time_point now, BYTE block);
  void        TransportEnded();
  uint64_t    Credit();
  void        ReportGate(bool available, uint64_t credit);
  void        Select(unsigned index);
  void        RejectFormats();
  void        Confirm(BYTE id, UINT16 timestamp);
  static void Activated(RdpsndServerContext* context);
  static UINT Confirmed(RdpsndServerContext* context, BYTE id, UINT16 timestamp);
  PeerLink&                                                                  _link;
  Diagnostics const&                                                         _diagnostics;
  EventQueue&                                                                _events;
  SessionAccess&                                                             _session;
  TraceQueue&                                                                _traces;
  std::unique_ptr<RdpsndServerContext, Releases<rdpsnd_server_context_free>> _sound;
  AUDIO_FORMAT                                                               _selected        { };
  bool                                                                       _rejected        { };
  bool                                                                       _gate_warned     { };
  bool                                                                       _ready           { };
  bool                                                                       _server_clock    { };
  bool                                                                       _has_confirmation{ };
  uint64_t                                                                   _sent            { };
  uint64_t                                                                   _confirmed       { };
  uint64_t                                                                   _clock_frames    { };
  Clock::time_point                                                          _first;
  Clock::time_point                                                          _clock_start;
  uint64_t                                                                   _blocks_sent     { };
  uint64_t                                                                   _gaps_over_40ms  { };
  Clock::time_point                                                          _last_send;
  Clock::duration                                                            _gap_total       { };
  Clock::duration                                                            _gap_max         { };
  std::deque<Block>                                                          _pending;
  std::vector<int16_t>                                                       _buffer;
};
}
