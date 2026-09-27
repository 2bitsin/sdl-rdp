#pragma once
#include <sdl-rdp/freerdp-facade/channel-manager.hpp>
#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/freerdp-facade/sound-channel-events.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

struct s_rdpsnd_server_context;

namespace sdl_rdp::freerdp_facade::detail::sound_channel {
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Releases;

// abi: the release step of an rdpsnd server context, handed the context.
auto ReleaseSound(s_rdpsnd_server_context* context) noexcept -> void;
using SoundContext = std::unique_ptr<s_rdpsnd_server_context, Releases<ReleaseSound>>;
inline constexpr std::string_view SoundChannelName{ "rdpsnd" };
// FreeRDP 3.32 fails a formats PDU listing none and a closed channel alike (rdpsnd_main.c:221,1224).
enum class SoundPump : std::uint8_t { Handled, FailedBeforeFormats, Failed };
// The rdpsnd static channel: the formats the server offers, the client's answer, and the sample blocks.
class SoundChannel : private Pinned {
public:
       SoundChannel(ChannelManager& channels, Connection& connection, SoundChannelEvents& events,
                    std::span<AudioFormat const> offered, std::chrono::milliseconds latency);
       ~SoundChannel();
  auto Initialize()                                                                -> bool;
  auto Pump()                                                                      -> SoundPump;
  auto Handle() const                                                              -> WaitHandle;
  auto Client() const                                                              -> SoundClient;
  auto Volume() const                                                              -> std::optional<std::uint32_t>;
  auto NextBlock() const                                                           -> std::uint8_t;
  auto Select(std::size_t index)                                                   -> AudioFormat;
  auto SendSamples(std::span<std::int16_t const> samples, std::uint16_t timestamp) -> bool;

private:
  class Slots;
  // The unit test drives the slots through the context.
  friend class SoundChannelProbe;
  static auto Pumped(std::uint32_t result, bool answered) -> SoundPump;
  auto        Context() const                             -> s_rdpsnd_server_context&;
  ChannelManager&     _channels;
  SoundChannelEvents& _events;
  SoundContext        _context;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::sound_channel::SoundChannel;
using detail::sound_channel::SoundChannelName;
using detail::sound_channel::SoundPump;
}
