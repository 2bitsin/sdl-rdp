#pragma once
#include "rdp-handles.hpp"

#include <chrono>
#include <deque>
#include <freerdp/server/rdpsnd.h>
#include <span>
#include <vector>

namespace Backend {
struct State;
class  Peer;
class AudioChannel {
public:
  using Clock = std::chrono::steady_clock;
                AudioChannel(AudioChannel const&) = delete;
                AudioChannel(AudioChannel&&)      = delete;
  explicit      AudioChannel(Peer& peer);
                ~AudioChannel();
  AudioChannel& operator = (AudioChannel const&)  = delete;
  AudioChannel& operator = (AudioChannel&&)       = delete;
  bool          Initialize();
  bool          Pump();
  HANDLE        Event() const;
  unsigned      Rate() const;
  unsigned      Remaining() const;
  void          Reset();
  void          LogAudio();
  void          AdoptServerClock();
  bool          Send(std::span<int16_t const> samples);

private:
  friend struct AudioProtocol;
  struct Block {
    BYTE              id    { };
    uint64_t          frames{ };
    Clock::time_point sent;
  };
  Peer&                                                                      peer;
  State&                                                                     owner;
  HANDLE                                                                     channels;
  std::unique_ptr<RdpsndServerContext, Releases<rdpsnd_server_context_free>> sound;
  AUDIO_FORMAT                                                               selected         { };
  bool                                                                       rejected         = false;
  bool                                                                       gate_warned      = false;
  bool                                                                       ready            = false;
  bool                                                                       server_clock     = false;
  bool                                                                       has_confirmation = false;
  uint64_t                                                                   sent             = 0;
  uint64_t                                                                   confirmed        = 0;
  uint64_t                                                                   clock_frames     = 0;
  Clock::time_point                                                          first;
  Clock::time_point                                                          clock_start;
  uint64_t                                                                   blocks_sent      = 0;
  uint64_t                                                                   gaps_over_40ms   = 0;
  Clock::time_point                                                          last_send;
  Clock::duration                                                            gap_total        { };
  Clock::duration                                                            gap_max          { };
  std::deque<Block>                                                          pending;
  std::vector<int16_t>                                                       buffer;
};
struct AudioProtocol {
public:
  static bool Ready(AudioChannel& self);
  static bool SendBlock(AudioChannel& self);
  static void RecordBlock(AudioChannel& self, AudioChannel::Clock::time_point now, BYTE block);
  static void TransportEnded(AudioChannel& self);

private:
  friend class AudioChannel;
  static bool     Supported(RdpsndServerContext const& context);
  static uint64_t Credit(AudioChannel& self);
  static void     ReportGate(AudioChannel& self, bool available, uint64_t credit);
  static void     Activated(RdpsndServerContext* context);
  static UINT     Confirmed(RdpsndServerContext* context, BYTE id, UINT16 timestamp);
  static void     Select(AudioChannel& self, unsigned index);
  static void     RejectFormats(AudioChannel& self);
};
} // namespace Backend
