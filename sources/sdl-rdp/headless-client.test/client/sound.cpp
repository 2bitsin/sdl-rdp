#include <sdl-rdp/headless-client.test/client/sound.hpp>

#include <sdl-rdp/headless-client.test/client/channels.hpp>
#include <sdl-rdp/headless-client.test/client/handles.hpp>
#include <sdl-rdp/headless-client.test/client/sound-protocol.hpp>
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/channels/channels.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace sdl_rdp::headless_client_test::client::detail::sound {
using sdl_rdp::headless_client_test::utilities::ObserverSet;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;

SoundClient::SoundClient(Client& target)
    : client(target), previous_load(ClientHandle(client).LoadChannels), membership(ClientContext(client), *this) {
  auto const playback = freerdp_settings_set_bool(ClientContext(client).settings, FreeRDP_AudioPlayback, true);
  Expects(playback, "sound playback enabled");
  // abi: pLoadChannels, BOOL is int
  ClientHandle(client).LoadChannels = [](freerdp* instance) -> int {
    Expects(instance != nullptr, "channel loading names its client");
    Expects(instance->context != nullptr, "the loading client has its context");
    auto const sound = ObserverSet::Of(*instance->context).Held<SoundClient>();
    if (sound->previous_load && !sound->previous_load(instance)) return false;
    return freerdp_channels_client_load_ex(instance->context->channels, instance->context->settings,
                                           SoundProtocol::Entry, &*sound)
           == 0;
  };
}
SoundClient::~SoundClient() {
  client.Disconnect();
  ClientHandle(client).LoadChannels = previous_load;
}
auto SoundClient::Send(std::span<std::byte const> bytes) -> bool {
  Expects(!bytes.empty(), "sound PDU is nonempty");
  return SendStaticChannel(*client.Instance(), "rdpsnd", bytes);
}
auto SoundClient::Capture(std::span<std::byte const> bytes) -> void {
  Expects(bytes.size() % 4 == 0, "PCM stereo frames complete");
  auto start = capture.samples.size();
  capture.samples.resize(start + (bytes.size() / 2));
  std::memcpy(capture.samples.data() + start, bytes.data(), bytes.size());
  capture.received.push_back(Clock::now());
  capture.pending.push_back({ timestamp, block, Narrowed<std::uint32_t>(bytes.size() / 4), capture.received.back() });
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
  if (!Send(std::as_bytes(std::span(bytes)))) return false;
  capture.confirmed_frames += confirmation.frames;
  capture.pending.erase(capture.pending.begin() + Narrowed<std::ptrdiff_t>(index));
  return true;
}
auto SoundClient::CaptureState() -> SoundCapture& {
  return capture;
}
auto SoundClient::CaptureState() const -> SoundCapture const& {
  return capture;
}
}
