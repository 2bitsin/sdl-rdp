#pragma once
#include "headless-client.hpp"

#include <cmath>
#include <deque>
#include <freerdp/channels/channels.h>
#include <freerdp/codec/audio.h>
#include <freerdp/svc.h>
#include <numbers>
#include <oxbox/utilities/serdes.hpp>
#include <span>
#include <winpr/stream.h>

namespace Headless {
inline std::pair<double, double> ToneMeasurements(std::vector<INT16> const& samples, unsigned rate) {
  auto start = std::ranges::find_if(samples, [](auto value) { return std::abs(value) > 100; }) - samples.begin();
  start += start % 2;
  Expects(start < samples.size(), "captured tone contains signal");
  auto frames = (samples.size() - start) / 2;
  Expects(frames > 1, "signal contains at least two frames");
  Expects(rate > 0, "sample rate is positive");
  unsigned crossings = 0;
  double   square    = 0;
  for (std::size_t frame = 1; frame < frames; ++frame) {
    auto sample = samples[start + (frame * 2)];
    crossings += samples[start + ((frame - 1) * 2)] <= 0 && sample > 0;
    square    += double(sample) * sample;
  }
  auto frequency = crossings * double(rate) / double(frames);
  auto db        = 20 * std::log10(std::sqrt(square / double(frames - 1)) * std::numbers::sqrt2 / 32767);
  return { frequency, db };
}
struct SoundCapture {
  struct Confirmation {
    UINT16            timestamp;
    BYTE              block;
    unsigned          frames;
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
class SoundClient;
struct SoundProtocol {
private:
  friend class SoundClient;
  static BOOL Register(CHANNEL_ENTRY_POINTS_EX* points, void* handle);
  static void Initialized(void* user, void* /*unused*/, UINT event, void* /*unused*/, UINT /*unused*/);
  static void Received(void* user, DWORD /*unused*/, UINT event, void* data, UINT32 size, UINT32 total, UINT32 flags);
  static void Formats(SoundClient& self, wStream* stream);
  static void Wave(SoundClient& self, wStream* stream, unsigned size, bool second);
  static void Receive(SoundClient& self);
  static void CaptureWave(SoundClient& self);
  static void Dispatch(SoundClient& self, wStream* stream, BYTE type, UINT16 size);
};
class SoundClient {
public:
  using Confirmation = SoundCapture::Confirmation;
  explicit SoundClient(Client& target) : client(target), previous_load(client.Instance()->LoadChannels) {
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
  SoundClient(SoundClient const&) = delete;
  SoundClient(SoundClient&&) = delete;
  ~SoundClient() {
    freerdp_disconnect(client.Instance().get());
    client.Instance()->LoadChannels = previous_load;
    active                          = nullptr;
  }
  SoundClient& operator =(SoundClient const&) = delete;
  SoundClient& operator =(SoundClient&&) = delete;

  bool Send(std::span<BYTE const> bytes) const {
    Expects(!bytes.empty(), "sound PDU is nonempty");
    auto* instance = client.Instance().get();
    auto  id       = freerdp_channels_get_id_by_name(instance, "rdpsnd");
    return id && instance->SendChannelData(instance, id, bytes.data(), bytes.size());
  }
  void Capture(std::span<BYTE const> bytes) {
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
  bool Confirm(std::size_t index = 0) {
    if (capture.pending.empty()) return true;
    Expects(index < capture.pending.size(), "confirmation identifies a received block");
    auto confirmation = capture.pending[index];
    std::array<BYTE, 8> bytes{
      5, 0, 4, 0, BYTE(confirmation.timestamp), BYTE(confirmation.timestamp >> 8), confirmation.block, 0
    };
    if (!Send(bytes)) return false;
    capture.confirmed_frames += confirmation.frames;
    capture.pending.erase(capture.pending.begin() + std::ptrdiff_t(index));
    return true;
  }

  SoundCapture& CaptureState() { return capture; }
  SoundCapture const& CaptureState() const { return capture; }

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

namespace Headless {
inline BOOL SoundProtocol::Register(CHANNEL_ENTRY_POINTS_EX* points, void* handle) {
  auto* extended = reinterpret_cast<CHANNEL_ENTRY_POINTS_FREERDP_EX*>(points);
  auto* self     = static_cast<SoundClient*>(extended->pExtendedData);
  Expects(self != nullptr, "capture context supplied");
  self->entry = *points;
  self->init  = handle;
  CHANNEL_DEF definition{ };
  std::memcpy(definition.name, "rdpsnd", 7);
  definition.options = CHANNEL_OPTION_INITIALIZED | CHANNEL_OPTION_ENCRYPT_RDP;
  return points->pVirtualChannelInitEx(self, nullptr, handle, &definition, 1, VIRTUAL_CHANNEL_VERSION_WIN2000,
                                       Initialized) == CHANNEL_RC_OK;
}
inline void SoundProtocol::Initialized(void* user, void* /*unused*/, UINT event, void* /*unused*/, UINT /*unused*/) {
  auto& self = *static_cast<SoundClient*>(user);
  if (event != CHANNEL_EVENT_CONNECTED) return;
  auto name = std::to_array("rdpsnd");
  Expects(self.entry.pVirtualChannelOpenEx(self.init, &self.channel, name.data(), Received) == CHANNEL_RC_OK,
          "sound static channel opens");
  self.capture.opened = true;
}
inline void SoundProtocol::Received(void* user, DWORD /*unused*/, UINT event, void* data, UINT32 size, UINT32 total,
                                    UINT32 flags) {
  auto& self = *static_cast<SoundClient*>(user);
  if (event != CHANNEL_EVENT_DATA_RECEIVED) return;
  if (flags & CHANNEL_FLAG_FIRST) {
    self.incoming.clear();
    self.incoming.reserve(total);
  }
  auto bytes = std::span(static_cast<BYTE*>(data), size);
  self.incoming.insert(self.incoming.end(), bytes.begin(), bytes.end());
  if (!(flags & CHANNEL_FLAG_LAST)) return;
  Expects(self.incoming.size() == total, "static channel fragments complete");
  Receive(self);
}

inline void ReadSoundFormats(wStream* stream, SoundCapture& capture) {
  Expects(Stream_GetRemainingLength(stream) >= 20, "server format header complete");
  Stream_Seek(stream, 14);
  UINT16 count = 0;
  Stream_Read_UINT16(stream, count);
  Stream_Seek(stream, 4);
  capture.server_formats.resize(count);
  std::ranges::for_each(capture.server_formats, [&](auto& format) {
    Expects(audio_format_read(stream, &format), "server format readable");
    Expects(format.cbSize == 0, "server announces plain PCM");
  });
}
inline std::vector<AUDIO_FORMAT> SupportedSoundFormats(SoundCapture const& capture) {
  AUDIO_FORMAT const        own      { WAVE_FORMAT_PCM, 2, capture.rate, capture.rate * 4, 4, 16, 0, nullptr };
  std::vector<AUDIO_FORMAT> supported;
  std::ranges::copy_if(capture.server_formats, std::back_inserter(supported), [&](auto const& format) {
    return capture.advertise_both_rates || audio_format_compatible(&own, &format);
  });
  if (supported.empty() && capture.advertise_unmatched) supported.push_back(own);
  return supported;
}
inline void WriteSoundFormatHeader(wStream* out, SoundCapture const& capture, std::size_t count, std::size_t size) {
  Expects(out != nullptr, "format reply stream exists");
  Expects(Stream_GetRemainingCapacity(out) >= 24, "format reply header fits");
  oxbox::utilities::BoundedWriter writer(
      std::as_writable_bytes(std::span(static_cast<std::byte*>(Stream_Pointer(out)), 24)));
  writer.Store<UINT8>(7);
  writer.Store<UINT8>(0);
  writer.Store<UINT16, std::endian::little>(UINT16(size - 4));
  writer.Store<UINT32, std::endian::little>(3);
  writer.Store<UINT32, std::endian::little>(capture.volume);
  writer.Store<UINT32, std::endian::little>(0);
  writer.Store<UINT16, std::endian::little>(0);
  writer.Store<UINT16, std::endian::little>(UINT16(count));
  writer.Store<UINT8>(0);
  writer.Store<UINT16, std::endian::little>(capture.version);
  writer.Store<UINT8>(0);
  Expects(writer.Whole(), "sound format header fills its wire layout");
  Stream_Seek(out, 24);
}
inline std::vector<BYTE> SoundFormatReply(SoundCapture const& capture) {
  auto supported = SupportedSoundFormats(capture);
  std::vector<BYTE> bytes(24 + (supported.size() * 18));
  wStream output { };
  auto*   out    = Stream_StaticInit(&output, bytes.data(), bytes.size());
  WriteSoundFormatHeader(out, capture, supported.size(), bytes.size());
  std::ranges::for_each(supported, [&](auto const& format) {
    Expects(audio_format_write(out, &format), "supported PCM format serialized");
  });
  return bytes;
}
inline void SoundProtocol::Formats(SoundClient& self, wStream* stream) {
  ReadSoundFormats(stream, self.capture);
  Expects(self.Send(SoundFormatReply(self.capture)), "client format intersection sent");
  if (self.capture.version >= 6)
    Expects(self.Send(std::array<BYTE, 8>{ 12, 0, 4, 0, 2, 0, 0, 0 }), "quality mode sent");
  self.capture.ready = true;
}
inline void SoundProtocol::Wave(SoundClient& self, wStream* stream, unsigned size, bool second) {
  Expects(Stream_GetRemainingLength(stream) >= 12, "wave header complete");
  UINT16 format = 0;
  Stream_Read_UINT16(stream, self.timestamp);
  Stream_Read_UINT16(stream, format);
  Stream_Read_UINT8(stream, self.block);
  Stream_Seek(stream, 3);
  Expects(format == 0, "announced PCM format selected");
  if (second) {
    Stream_Seek(stream, 4);
    Expects(size >= 12, "wave PDU includes its fixed header");
    Expects(Stream_GetRemainingLength(stream) >= size - 12, "wave payload fits the remaining stream");
    self.Capture({ reinterpret_cast<BYTE const*>(Stream_Pointer(stream)), size - 12 });
  } else {
    Stream_Read(stream, self.first.data(), self.first.size());
    self.wave_bytes     = size - 8;
    self.expecting_wave = true;
  }
}
inline void SoundProtocol::Receive(SoundClient& self) {
  if (self.expecting_wave) {
    CaptureWave(self);
    return;
  }
  Expects(self.incoming.size() >= 4, "sound PDU header complete");
  wStream storage { };
  auto*   stream  = Stream_StaticInit(&storage, self.incoming.data(), self.incoming.size());
  BYTE    type    = 0;
  UINT16  size    = 0;
  Stream_Read_UINT8(stream, type);
  Stream_Seek(stream, 1);
  Stream_Read_UINT16(stream, size);
  Dispatch(self, stream, type, size);
}
}

namespace Headless {
inline void SoundProtocol::CaptureWave(SoundClient& self) {
  Expects(self.incoming.size() >= self.wave_bytes, "complete wave payload is buffered");
  Expects(self.wave_bytes >= 4, "wave payload includes its prefix");
  std::ranges::copy(self.first, self.incoming.begin());
  self.expecting_wave = false;
  self.Capture(std::span(self.incoming).first(self.wave_bytes));
}
inline void SoundProtocol::Dispatch(SoundClient& self, wStream* stream, BYTE type, UINT16 size) {
  if (type == 7)
    Formats(self, stream);
  else if (type == 2 || type == 13)
    Wave(self, stream, size, type == 13);
  else if (type == 6) {
    Expects(self.incoming.size() >= 8, "training request has its header");
    Expects(self.Send(std::span(self.incoming).first(8)), "training response is sent");
  } else if (type != 1 && type != 3)
    utilities::Unreachable(type);
}
}
