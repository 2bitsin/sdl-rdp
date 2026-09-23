#pragma once
#include "headless-client.hpp"
#include <freerdp/svc.h>
#include <freerdp/channels/channels.h>
#include <freerdp/codec/audio.h>
#include <winpr/stream.h>
#include <deque>
#include <span>
#include <cmath>
#include <numbers>

namespace Headless {
inline std::pair<double, double> ToneMeasurements(std::vector<INT16> const& samples, unsigned rate) {
  auto start = std::ranges::find_if(samples, [](auto value) { return std::abs(value) > 100; }) - samples.begin();
  start += start % 2;
  Expects(start < samples.size(), "captured tone contains signal");
  auto frames = (samples.size() - start) / 2;
  Expects(frames > 1 && rate > 0, "signal has frames and a sample rate");
  unsigned crossings = 0;
  double square = 0;
  for (std::size_t frame = 1; frame < frames; ++frame) {
    auto sample = samples[start + frame * 2];
    crossings += samples[start + (frame - 1) * 2] <= 0 && sample > 0;
    square += double(sample) * sample;
  }
  auto frequency = crossings * double(rate) / frames;
  auto db = 20 * std::log10(std::sqrt(square / (frames - 1)) * std::sqrt(2.0) / 32767);
  return {frequency, db};
}
class SoundClient {
  inline static thread_local SoundClient* active = nullptr;
  Client& client;
  decltype(freerdp::LoadChannels) previous_load = nullptr;
  CHANNEL_ENTRY_POINTS_EX entry{};
  void* init = nullptr;
  DWORD channel = 0;
  std::vector<BYTE> incoming;
  std::array<BYTE, 4> first{};
  unsigned wave_bytes = 0;
  UINT16 timestamp = 0;
  BYTE block = 0;
  bool expecting_wave = false;
public:
  struct Confirmation { UINT16 timestamp; BYTE block; unsigned frames; Clock::time_point received; };
  std::vector<INT16> samples;
  std::vector<Clock::time_point> received;
  std::vector<AUDIO_FORMAT> server_formats;
  std::deque<Confirmation> pending;
  unsigned rate = 44100, version = 8, volume = 0xffffffffu;
  std::size_t confirmed_frames = 0, maximum_pending_frames = 0;
  bool advertise_unmatched = false, advertise_both_rates = false;
  bool ready = false, auto_confirm = true, opened = false;
  explicit SoundClient(Client& target) : client(target) {
    Expects(freerdp_settings_set_bool(client.instance->context->settings, FreeRDP_AudioPlayback, TRUE), "sound playback enabled");
    Expects(!active, "one sound capture per client thread");
    active = this;
    previous_load = client.instance->LoadChannels;
    client.instance->LoadChannels = [](freerdp* instance) -> BOOL {
      if (active->previous_load && !active->previous_load(instance)) return FALSE;
      return freerdp_channels_client_load_ex(instance->context->channels,
        instance->context->settings, Register, active) == 0;
    };
  }
  ~SoundClient() {
    freerdp_disconnect(client.instance.get());
    client.instance->LoadChannels = previous_load;
    active = nullptr;
  }
  SoundClient(SoundClient const&) = delete;
  static BOOL Register(CHANNEL_ENTRY_POINTS_EX* points, void* handle) {
    auto extended = reinterpret_cast<CHANNEL_ENTRY_POINTS_FREERDP_EX*>(points);
    auto self = static_cast<SoundClient*>(extended->pExtendedData);
    Expects(self != nullptr, "capture context supplied");
    self->entry = *points;
    self->init = handle;
    CHANNEL_DEF definition{};
    std::memcpy(definition.name, "rdpsnd", 7);
    definition.options = CHANNEL_OPTION_INITIALIZED | CHANNEL_OPTION_ENCRYPT_RDP;
    return points->pVirtualChannelInitEx(self, nullptr, handle, &definition, 1,
      VIRTUAL_CHANNEL_VERSION_WIN2000, Initialized) == CHANNEL_RC_OK;
  }
  static void Initialized(void* user, void*, UINT event, void*, UINT) {
    auto& self = *static_cast<SoundClient*>(user);
    if (event != CHANNEL_EVENT_CONNECTED) return;
    char name[] = "rdpsnd";
    Expects(self.entry.pVirtualChannelOpenEx(self.init, &self.channel, name, Received) == CHANNEL_RC_OK,
      "sound static channel opens");
    self.opened = true;
  }
  static void Received(void* user, DWORD, UINT event, void* data, UINT32 size, UINT32 total, UINT32 flags) {
    auto& self = *static_cast<SoundClient*>(user);
    if (event != CHANNEL_EVENT_DATA_RECEIVED) return;
    if (flags & CHANNEL_FLAG_FIRST) { self.incoming.clear(); self.incoming.reserve(total); }
    auto bytes = std::span(static_cast<BYTE*>(data), size);
    self.incoming.insert(self.incoming.end(), bytes.begin(), bytes.end());
    if (!(flags & CHANNEL_FLAG_LAST)) return;
    Expects(self.incoming.size() == total, "static channel fragments complete");
    self.Receive();
  }
  bool Send(std::span<BYTE const> bytes) {
    Expects(!bytes.empty(), "sound PDU is nonempty");
    auto instance = client.instance.get();
    auto id = freerdp_channels_get_id_by_name(instance, "rdpsnd");
    return id && instance->SendChannelData(instance, id, bytes.data(), bytes.size());
  }
  void Formats(wStream* stream) {
    Expects(Stream_GetRemainingLength(stream) >= 20, "server format header complete");
    Stream_Seek(stream, 14);
    UINT16 count = 0;
    Stream_Read_UINT16(stream, count);
    Stream_Seek(stream, 4);
    server_formats.resize(count);
    for (auto& format : server_formats)
      Expects(audio_format_read(stream, &format) && format.cbSize == 0, "server announces plain PCM");
    AUDIO_FORMAT own{WAVE_FORMAT_PCM, 2, rate, rate * 4, 4, 16, 0, nullptr};
    std::vector<AUDIO_FORMAT> supported;
    for (auto const& format : server_formats)
      if (advertise_both_rates || audio_format_compatible(&own, &format)) supported.push_back(format);
    if (supported.empty() && advertise_unmatched) supported.push_back(own);
    std::vector<BYTE> bytes(24 + supported.size() * 18);
    wStream output{};
    auto out = Stream_StaticInit(&output, bytes.data(), bytes.size());
    Stream_Write_UINT8(out, 7); Stream_Write_UINT8(out, 0); Stream_Write_UINT16(out, bytes.size() - 4);
    Stream_Write_UINT32(out, 3); Stream_Write_UINT32(out, volume); Stream_Write_UINT32(out, 0);
    Stream_Write_UINT16(out, 0); Stream_Write_UINT16(out, supported.size()); Stream_Write_UINT8(out, 0);
    Stream_Write_UINT16(out, version); Stream_Write_UINT8(out, 0);
    for (auto const& format : supported)
      Expects(audio_format_write(out, &format), "supported PCM format serialized");
    Expects(Send(bytes), "client format intersection sent");
    if (version >= 6) Expects(Send(std::array<BYTE, 8>{12, 0, 4, 0, 2, 0, 0, 0}), "quality mode sent");
    ready = true;
  }
  void Capture(std::span<BYTE const> bytes) {
    Expects(bytes.size() % 4 == 0, "PCM stereo frames complete");
    auto start = samples.size();
    samples.resize(start + bytes.size() / 2);
    std::memcpy(samples.data() + start, bytes.data(), bytes.size());
    received.push_back(Clock::now());
    pending.push_back({timestamp, block, unsigned(bytes.size() / 4), received.back()});
    maximum_pending_frames = std::max(maximum_pending_frames, samples.size() / 2 - confirmed_frames);
    if (auto_confirm) Expects(Confirm(), "wave confirmation sent");
  }
  bool Confirm(std::size_t index = 0) {
    if (pending.empty()) return true;
    Expects(index < pending.size(), "confirmation identifies a received block");
    auto confirmation = pending[index];
    std::array<BYTE, 8> bytes{5, 0, 4, 0, BYTE(confirmation.timestamp), BYTE(confirmation.timestamp >> 8), confirmation.block, 0};
    if (!Send(bytes)) return false;
    confirmed_frames += confirmation.frames;
    pending.erase(pending.begin() + index);
    return true;
  }
  void Wave(wStream* stream, unsigned size, bool second) {
    Expects(Stream_GetRemainingLength(stream) >= 12, "wave header complete");
    UINT16 format = 0;
    Stream_Read_UINT16(stream, timestamp); Stream_Read_UINT16(stream, format);
    Stream_Read_UINT8(stream, block); Stream_Seek(stream, 3);
    Expects(format == 0, "announced PCM format selected");
    if (second) {
      Stream_Seek(stream, 4);
      Expects(size >= 12 && Stream_GetRemainingLength(stream) >= size - 12, "wave two payload complete");
      Capture({reinterpret_cast<BYTE const*>(Stream_Pointer(stream)), size - 12});
    } else {
      Stream_Read(stream, first.data(), first.size());
      wave_bytes = size - 8;
      expecting_wave = true;
    }
  }
  void Receive() {
    if (expecting_wave) {
      Expects(incoming.size() >= wave_bytes && wave_bytes >= 4, "wave payload complete");
      std::ranges::copy(first, incoming.begin());
      expecting_wave = false;
      Capture(std::span(incoming).first(wave_bytes));
      return;
    }
    Expects(incoming.size() >= 4, "sound PDU header complete");
    wStream storage{};
    auto stream = Stream_StaticInit(&storage, incoming.data(), incoming.size());
    BYTE type = 0;
    UINT16 size = 0;
    Stream_Read_UINT8(stream, type); Stream_Seek(stream, 1); Stream_Read_UINT16(stream, size);
    switch (type) {
      case 7: Formats(stream); break;
      case 2: Wave(stream, size, false); break;
      case 13: Wave(stream, size, true); break;
      case 6: Expects(incoming.size() >= 8 && Send(std::span(incoming).first(8)), "training confirmed"); break;
      case 1: case 3: break;
      default: utilities::Unreachable(type);
    }
  }
};
}
