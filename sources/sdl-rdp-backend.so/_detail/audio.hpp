#pragma once
#include "rdp-handles.hpp"
#include <freerdp/server/rdpsnd.h>
#include <chrono>
#include <deque>
#include <span>
#include <vector>

namespace Backend {
struct State;
class Peer;
class AudioChannel {
public:
  using Clock = std::chrono::steady_clock;
  explicit AudioChannel(Peer &peer);
  ~AudioChannel();
  bool Initialize();
  bool Pump();
  HANDLE Event() const;
  unsigned Rate() const;
  unsigned Remaining() const;
  void Reset();
  void LogAudio();
  void AdoptServerClock();
  bool Ready();
  bool Send(std::span<int16_t const> samples);

private:
  struct Block {
    BYTE id{};
    uint64_t frames{};
    Clock::time_point sent;
  };
  static void Activated(RdpsndServerContext * /*context*/);
  static UINT Confirmed(RdpsndServerContext * /*context*/, BYTE /*id*/, UINT16 /*timestamp*/);
  void Select(unsigned index);
  void RejectFormats();
  Peer &peer;
  State &owner;
  HANDLE channels;
  std::unique_ptr<RdpsndServerContext, Releases<rdpsnd_server_context_free>> sound;
  AUDIO_FORMAT selected{};
  bool rejected = false, gate_warned = false;
  bool ready = false, server_clock = false, has_confirmation = false;
  uint64_t sent = 0, confirmed = 0, clock_frames = 0;
  Clock::time_point first, clock_start;
  uint64_t blocks_sent = 0, gaps_over_40ms = 0;
  Clock::time_point last_send;
  Clock::duration gap_total{}, gap_max{};
  std::deque<Block> pending;
  std::vector<int16_t> buffer;
};
} // namespace Backend
