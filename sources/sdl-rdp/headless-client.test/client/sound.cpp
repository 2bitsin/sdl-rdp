#include <sdl-rdp/headless-client.test/client/sound.hpp>

#include <sdl-rdp/headless-client.test/client/channels.hpp>
#include <sdl-rdp/headless-client.test/client/sound-protocol.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/channels/channels.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Headless {
SoundClient::SoundClient(Client& target) : client(target), previous_load(client.Instance()->LoadChannels) {
  auto const playback = freerdp_settings_set_bool(client.Instance()->context->settings, FreeRDP_AudioPlayback, true);
  Expects(playback, "sound playback enabled");
  Expects(!active, "one sound capture per client thread");
  active = this;

  // abi: pLoadChannels, BOOL is int
  client.Instance()->LoadChannels = [](freerdp* instance) -> int {
    if (active->previous_load && !active->previous_load(instance)) return false;
    return freerdp_channels_client_load_ex(instance->context->channels, instance->context->settings,
                                           SoundProtocol::EntryPoint(), active)
           == 0;
  };
}
SoundClient::~SoundClient() {
  client.Disconnect();
  client.Instance()->LoadChannels = previous_load;
  active                          = nullptr;
}
auto SoundClient::Send(std::span<std::uint8_t const> bytes) -> bool {
  Expects(!bytes.empty(), "sound PDU is nonempty");
  return SendStaticChannel(client.Instance().get(), "rdpsnd", bytes);
}
auto SoundClient::Capture(std::span<std::uint8_t const> bytes) -> void {
  Expects(bytes.size() % 4 == 0, "PCM stereo frames complete");
  auto start = capture.samples.size();
  capture.samples.resize(start + (bytes.size() / 2));
  std::memcpy(capture.samples.data() + start, bytes.data(), bytes.size());
  capture.received.push_back(Clock::now());
  capture.pending.push_back(
      { timestamp, block, Backend::Narrowed<std::uint32_t>(bytes.size() / 4), capture.received.back() });
  capture.maximum_pending_frames = std::max(capture.maximum_pending_frames,
                                            (capture.samples.size() / 2) - capture.confirmed_frames);
  if (!capture.auto_confirm) return;
  auto const confirmed = Confirm();
  Expects(confirmed, "wave confirmation sent");
}
auto SoundClient::Confirm(std::size_t index) -> bool {
  if (capture.pending.empty()) return true;
  Expects(index < capture.pending.size(), "confirmation identifies a received block");
  auto confirmation = capture.pending[index];
  // MS-RDPEA 2.2.3.8 carries the timestamp little-endian: its low byte, then its high byte.
  std::array<std::uint8_t, 8> bytes{ 5,
                                     0,
                                     4,
                                     0,
                                     static_cast<std::uint8_t>(confirmation.timestamp),
                                     static_cast<std::uint8_t>(confirmation.timestamp >> 8),
                                     confirmation.block,
                                     0 };
  if (!Send(bytes)) return false;
  capture.confirmed_frames += confirmation.frames;
  capture.pending.erase(capture.pending.begin() + Backend::Narrowed<std::ptrdiff_t>(index));
  return true;
}
auto SoundClient::CaptureState() -> SoundCapture& {
  return capture;
}
auto SoundClient::CaptureState() const -> SoundCapture const& {
  return capture;
}
}
