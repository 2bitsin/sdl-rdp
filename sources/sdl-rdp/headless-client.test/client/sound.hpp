#pragma once
#include "client.hpp"

#include <freerdp/codec/audio.h>
#include <winpr/wtsapi.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace Headless {
struct SoundCapture {
  struct Confirmation {
    std::uint16_t     timestamp = 0;
    std::uint8_t      block     = 0;
    std::size_t       frames    = 0;
    Clock::time_point received;
  };
  std::vector<std::int16_t>      samples;
  std::vector<Clock::time_point> received;
  std::vector<AUDIO_FORMAT>      server_formats;
  std::deque<Confirmation>       pending;
  std::uint32_t                  rate                   = 44100;
  std::uint32_t                  version                = 8;
  std::uint32_t                  volume                 = 0xffffffffu;
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
           SoundClient(SoundClient const&)               = delete;
           SoundClient(SoundClient&&)                    = delete;
           ~SoundClient();
  auto     operator=(SoundClient const&) -> SoundClient& = delete;
  auto     operator=(SoundClient&&)      -> SoundClient& = delete;

  auto Send(std::span<std::uint8_t const> bytes)    -> bool;
  auto Capture(std::span<std::uint8_t const> bytes) -> void;
  auto Confirm(std::size_t index = 0)               -> bool;

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
  std::uint32_t                           channel        = 0;
  std::vector<std::uint8_t>               incoming;
  std::array<std::uint8_t, 4>             first          { };
  std::uint32_t                           wave_bytes     = 0;
  std::uint16_t                           timestamp      = 0;
  std::uint8_t                            block          = 0;
  bool                                    expecting_wave = false;
};
}
