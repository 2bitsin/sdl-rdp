#pragma once
#include <sdl-rdp/freerdp-facade/failure-sink.hpp>

#include <cstdint>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::sound_channel_events {
// WAVE_FORMAT_PCM (RFC 2361), the format tag of uncompressed samples.
inline constexpr std::uint16_t WavePcm = 1;
struct AudioFormat {
  std::uint16_t tag     { };
  std::uint16_t channels{ };
  std::uint32_t rate    { };
  std::uint16_t bits    { };
};
struct SoundClient {
  std::uint16_t            version{ };
  std::vector<AudioFormat> formats;
};
struct BlockConfirm {
  std::uint8_t  block    { };
  std::uint16_t timestamp{ };
};
// What an rdpsnd channel's slots report: the client's formats arrived, and the client played a block.
class SoundChannelEvents : public FailureSink {
public:
  virtual auto Activated(SoundClient const& client) -> void = 0;
  virtual auto Confirmed(BlockConfirm confirm)      -> void = 0;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::sound_channel_events::AudioFormat;
using detail::sound_channel_events::BlockConfirm;
using detail::sound_channel_events::SoundChannelEvents;
using detail::sound_channel_events::SoundClient;
using detail::sound_channel_events::WavePcm;
}
