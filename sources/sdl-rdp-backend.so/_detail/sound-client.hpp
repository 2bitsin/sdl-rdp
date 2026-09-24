#pragma once
#include "client.hpp"

#include <array>
#include <deque>
#include <freerdp/codec/audio.h>
#include <span>
#include <vector>
#include <winpr/wtsapi.h>

namespace Headless {
struct SoundCapture {
  struct Confirmation {
    UINT16            timestamp = 0;
    BYTE              block     = 0;
    unsigned          frames    = 0;
    Clock::time_point received;
  };
  std::vector<INT16>             samples;
  std::vector<Clock::time_point> received;
  std::vector<AUDIO_FORMAT>      server_formats;
  std::deque<Confirmation>       pending;
  unsigned                       rate                   = 44100;
  unsigned                       version                = 8;
  unsigned                       volume                 = 0xffffffffu;
  std::size_t                    confirmed_frames       = 0;
  std::size_t                    maximum_pending_frames = 0;
  bool                           advertise_unmatched    = false;
  bool                           advertise_both_rates   = false;
  bool                           ready                  = false;
  bool                           auto_confirm           = true;
  bool                           opened                 = false;
};
class SoundClient {
public:
  using Confirmation = SoundCapture::Confirmation;
  explicit SoundClient(Client& target);
           SoundClient(SoundClient const&)                 = delete;
           SoundClient(SoundClient&&)                      = delete;
           ~SoundClient();
  auto     operator = (SoundClient const&) -> SoundClient& = delete;
  auto     operator = (SoundClient&&)      -> SoundClient& = delete;

  auto Send(std::span<BYTE const> bytes) const -> bool;
  auto Capture(std::span<BYTE const> bytes)    -> void;
  auto Confirm(std::size_t index = 0)          -> bool;

  auto CaptureState()       -> SoundCapture&;
  auto CaptureState() const -> SoundCapture const&;

private:
  friend struct                           SoundProtocol;
  SoundCapture                            capture;
  inline static thread_local SoundClient* active         = nullptr;
  Client&                                 client;
  decltype(freerdp::LoadChannels)         previous_load  = nullptr;
  CHANNEL_ENTRY_POINTS_EX                 entry          { };
  void*                                   init           = nullptr;
  DWORD                                   channel        = 0;
  std::vector<BYTE>                       incoming;
  std::array<BYTE, 4>                     first          { };
  unsigned                                wave_bytes     = 0;
  UINT16                                  timestamp      = 0;
  BYTE                                    block          = 0;
  bool                                    expecting_wave = false;
};
}
