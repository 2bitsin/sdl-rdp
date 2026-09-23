#pragma once
#include "rdp-handles.hpp"
#include <freerdp/server/rdpsnd.h>
#include <chrono>
#include <deque>
#include <span>
#include <vector>

namespace Backend {
struct State;
class AudioChannel {
public:
  using Clock = std::chrono::steady_clock;
  AudioChannel(State& owner, HANDLE channels, rdpContext* context, HANDLE wake);
  bool Initialize();
  bool Pump();
  HANDLE Event() const;
  unsigned Rate() const;
  unsigned Remaining() const;
  void Reset();
  void AdoptServerClock();
  bool Ready();
  bool Send(std::span<int16_t const> samples);
private:
  static void Activated(RdpsndServerContext*);
  static UINT Confirmed(RdpsndServerContext*, BYTE, UINT16);
  void Select(unsigned index);
  void RejectFormats();
  State& owner;
  HANDLE wake;
  std::unique_ptr<RdpsndServerContext, Releases<rdpsnd_server_context_free>> sound;
  AUDIO_FORMAT selected{};
  bool rejected = false, gate_warned = false;
  bool ready = false, server_clock = false, has_confirmation = false;
  uint64_t sent = 0, confirmed = 0, clock_frames = 0;
  Clock::time_point first{}, clock_start{};
  struct Block { BYTE id; uint64_t frames; Clock::time_point sent; };
  std::deque<Block> pending;
  std::vector<int16_t> buffer;
};
}
