#include "_detail/sound-client.hpp"

#include "_detail/client-channels.hpp"
#include "_detail/sound-protocol.hpp"

#include <algorithm>
#include <cstring>
#include <freerdp/channels/channels.h>

namespace Headless {
SoundClient::SoundClient(Client& target) : client(target), previous_load(client.Instance()->LoadChannels) {
  Expects(freerdp_settings_set_bool(client.Instance()->context->settings, FreeRDP_AudioPlayback, TRUE),
          "sound playback enabled");
  Expects(!active, "one sound capture per client thread");
  active = this;

  client.Instance()->LoadChannels = [](freerdp* instance) -> BOOL {
    if (active->previous_load && !active->previous_load(instance)) return FALSE;
    return freerdp_channels_client_load_ex(instance->context->channels, instance->context->settings,
                                           SoundProtocol::Register, active) == 0;
  };
}
SoundClient::~SoundClient() {
  freerdp_disconnect(client.Instance().get());
  client.Instance()->LoadChannels = previous_load;
  active                          = nullptr;
}
bool SoundClient::Send(std::span<BYTE const> bytes) const {
  Expects(!bytes.empty(), "sound PDU is nonempty");
  return SendStaticChannel(client.Instance().get(), "rdpsnd", bytes);
}
void SoundClient::Capture(std::span<BYTE const> bytes) {
  Expects(bytes.size() % 4 == 0, "PCM stereo frames complete");
  auto start = capture.samples.size();
  capture.samples.resize(start + (bytes.size() / 2));
  std::memcpy(capture.samples.data() + start, bytes.data(), bytes.size());
  capture.received.push_back(Clock::now());
  capture.pending.push_back({ timestamp, block, unsigned(bytes.size() / 4), capture.received.back() });
  capture.maximum_pending_frames =
      std::max(capture.maximum_pending_frames, (capture.samples.size() / 2) - capture.confirmed_frames);
  if (capture.auto_confirm) Expects(Confirm(), "wave confirmation sent");
}
bool SoundClient::Confirm(std::size_t index) {
  if (capture.pending.empty()) return true;
  Expects(index < capture.pending.size(), "confirmation identifies a received block");
  auto                confirmation = capture.pending[index];
  std::array<BYTE, 8> bytes        {
    5, 0, 4, 0, BYTE(confirmation.timestamp), BYTE(confirmation.timestamp >> 8), confirmation.block, 0
  };
  if (!Send(bytes)) return false;
  capture.confirmed_frames += confirmation.frames;
  capture.pending.erase(capture.pending.begin() + std::ptrdiff_t(index));
  return true;
}
SoundCapture& SoundClient::CaptureState() {
  return capture;
}
SoundCapture const& SoundClient::CaptureState() const {
  return capture;
}
}
