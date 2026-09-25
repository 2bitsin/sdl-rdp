#include <sdl-rdp/headless-client.test/client/sound-protocol.hpp>

#include <sdl-rdp/headless-client.test/client/sound.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/svc.h>
#include <oxbox/utilities/serdes.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>

namespace Headless {
namespace {
auto ReadSoundFormats(wStream* stream, SoundCapture& capture) -> void {
  auto const remaining = Stream_GetRemainingLength(stream);
  Expects(remaining >= 20, "server format header complete");
  Stream_Seek(stream, 14);
  std::uint16_t count = 0;
  Stream_Read_UINT16(stream, count);
  Stream_Seek(stream, 4);
  capture.server_formats.resize(count);
  std::ranges::for_each(capture.server_formats, [&](auto& format) {
    auto const read = audio_format_read(stream, &format);
    Expects(read, "server format readable");
    Expects(format.cbSize == 0, "server announces plain PCM");
  });
}
auto SupportedSoundFormats(SoundCapture const& capture) -> std::vector<AUDIO_FORMAT> {
  AUDIO_FORMAT const        own      { WAVE_FORMAT_PCM, 2, capture.rate, capture.rate * 4, 4, 16, 0, nullptr };
  std::vector<AUDIO_FORMAT> supported;
  std::ranges::copy_if(capture.server_formats, std::back_inserter(supported), [&](auto const& format) {
    return capture.advertise_both_rates || audio_format_compatible(&own, &format);
  });
  if (supported.empty() && capture.advertise_unmatched) supported.push_back(own);
  return supported;
}
auto WriteSoundFormatHeader(wStream* out, SoundCapture const& capture, std::size_t count, std::size_t size) -> void {
  Expects(out != nullptr, "format reply stream exists");
  auto const capacity = Stream_GetRemainingCapacity(out);
  Expects(capacity >= 24, "format reply header fits");
  oxbox::utilities::BoundedWriter writer(
      std::as_writable_bytes(std::span(static_cast<std::byte*>(Stream_Pointer(out)), 24)));
  writer.Store<std::uint8_t>(7);
  writer.Store<std::uint8_t>(0);
  writer.Store<std::uint16_t, std::endian::little>(Backend::Narrowed<std::uint16_t>(size - 4));
  writer.Store<std::uint32_t, std::endian::little>(3);
  writer.Store<std::uint32_t, std::endian::little>(capture.volume);
  writer.Store<std::uint32_t, std::endian::little>(0);
  writer.Store<std::uint16_t, std::endian::little>(0);
  writer.Store<std::uint16_t, std::endian::little>(Backend::Narrowed<std::uint16_t>(count));
  writer.Store<std::uint8_t>(0);
  writer.Store<std::uint16_t, std::endian::little>(capture.version);
  writer.Store<std::uint8_t>(0);
  auto const filled = writer.Whole();
  Expects(filled, "sound format header fills its wire layout");
  Stream_Seek(out, 24);
}
auto SoundFormatReply(SoundCapture const& capture) -> std::vector<std::uint8_t> {
  auto                      supported = SupportedSoundFormats(capture);
  std::vector<std::uint8_t> bytes(24 + (supported.size() * 18));
  wStream                   output    { };
  auto*                     out       = Stream_StaticInit(&output, bytes.data(), bytes.size());
  WriteSoundFormatHeader(out, capture, supported.size(), bytes.size());
  std::ranges::for_each(supported, [&](auto const& format) {
    auto const written = audio_format_write(out, &format);
    Expects(written, "supported PCM format serialized");
  });
  return bytes;
}
}

auto SoundProtocol::EntryPoint() -> PVIRTUALCHANNELENTRYEX {
  // abi: VIRTUALCHANNELENTRYEX, BOOL is int
  return [](CHANNEL_ENTRY_POINTS_EX* points, void* handle) -> int {
    Expects(points != nullptr, "channel entry points supplied");
    auto* extended = reinterpret_cast<CHANNEL_ENTRY_POINTS_FREERDP_EX*>(points);
    auto* self     = static_cast<SoundClient*>(extended->pExtendedData);
    Expects(self != nullptr, "capture context supplied");
    return Register(*self, *points, handle);
  };
}
auto SoundProtocol::Register(SoundClient& self, CHANNEL_ENTRY_POINTS_EX const& points, void* handle) -> bool {
  self.entry = points;
  self.init  = handle;
  CHANNEL_DEF definition{ };
  std::memcpy(definition.name, "rdpsnd", 7);
  definition.options = CHANNEL_OPTION_INITIALIZED | CHANNEL_OPTION_ENCRYPT_RDP;
  // abi: CHANNEL_INIT_EVENT_EX_FN, UINT is uint32_t
  auto const initialized = [](void* user, void*, std::uint32_t event, void*, std::uint32_t) {
    Initialized(*static_cast<SoundClient*>(user), event);
  };
  return points.pVirtualChannelInitEx(&self, nullptr, handle, &definition, 1, VIRTUAL_CHANNEL_VERSION_WIN2000,
                                      initialized)
         == CHANNEL_RC_OK;
}
auto SoundProtocol::Initialized(SoundClient& self, std::uint32_t event) -> void {
  if (event != CHANNEL_EVENT_CONNECTED) return;
  auto name = std::to_array("rdpsnd");
  // abi: CHANNEL_OPEN_EVENT_EX_FN, DWORD, UINT and UINT32 are uint32_t
  auto const received = [](void* user, std::uint32_t, std::uint32_t event, void* data, std::uint32_t size,
                           std::uint32_t total, std::uint32_t flags) {
    if (event != CHANNEL_EVENT_DATA_RECEIVED) return;
    Received(*static_cast<SoundClient*>(user), std::span(static_cast<std::uint8_t const*>(data), size), total, flags);
  };
  auto const opened   = self.entry.pVirtualChannelOpenEx(self.init, &self.channel, name.data(), received);
  Expects(opened == CHANNEL_RC_OK, "sound static channel opens");
  self.capture.opened = true;
}
auto SoundProtocol::Received(SoundClient& self, std::span<std::uint8_t const> bytes, std::size_t total,
                             std::uint32_t flags) -> void {
  if (flags & CHANNEL_FLAG_FIRST) {
    self.incoming.clear();
    self.incoming.reserve(total);
  }
  self.incoming.insert(self.incoming.end(), bytes.begin(), bytes.end());
  if (!(flags & CHANNEL_FLAG_LAST)) return;
  Expects(self.incoming.size() == total, "static channel fragments complete");
  Receive(self);
}
auto SoundProtocol::Formats(SoundClient& self, wStream* stream) -> void {
  ReadSoundFormats(stream, self.capture);
  auto const replied = self.Send(SoundFormatReply(self.capture));
  Expects(replied, "client format intersection sent");
  if (self.capture.version >= 6) {
    auto const quality_sent = self.Send(std::array<std::uint8_t, 8>{ 12, 0, 4, 0, 2, 0, 0, 0 });
    Expects(quality_sent, "quality mode sent");
  }
  self.capture.ready = true;
}
auto SoundProtocol::Wave(SoundClient& self, wStream* stream, std::uint32_t size, bool second) -> void {
  auto const remaining = Stream_GetRemainingLength(stream);
  Expects(remaining >= 12, "wave header complete");
  std::uint16_t format = 0;
  Stream_Read_UINT16(stream, self.timestamp);
  Stream_Read_UINT16(stream, format);
  Stream_Read_UINT8(stream, self.block);
  Stream_Seek(stream, 3);
  Expects(format == 0, "announced PCM format selected");
  if (second) {
    Stream_Seek(stream, 4);
    Expects(size >= 12, "wave PDU includes its fixed header");
    auto const remaining_after_header = Stream_GetRemainingLength(stream);
    Expects(remaining_after_header >= size - 12, "wave payload fits the remaining stream");
    self.Capture({ static_cast<std::uint8_t const*>(Stream_Pointer(stream)), size - 12 });
  } else {
    Stream_Read(stream, self.first.data(), self.first.size());
    self.wave_bytes     = size - 8;
    self.expecting_wave = true;
  }
}
auto SoundProtocol::Receive(SoundClient& self) -> void {
  if (self.expecting_wave) {
    CaptureWave(self);
    return;
  }
  Expects(self.incoming.size() >= 4, "sound PDU header complete");
  wStream       storage { };
  auto*         stream  = Stream_StaticInit(&storage, self.incoming.data(), self.incoming.size());
  std::uint8_t  type    = 0;
  std::uint16_t size    = 0;
  Stream_Read_UINT8(stream, type);
  Stream_Seek(stream, 1);
  Stream_Read_UINT16(stream, size);
  Dispatch(self, stream, type, size);
}
auto SoundProtocol::CaptureWave(SoundClient& self) -> void {
  Expects(self.incoming.size() >= self.wave_bytes, "complete wave payload is buffered");
  Expects(self.wave_bytes >= 4, "wave payload includes its prefix");
  std::ranges::copy(self.first, self.incoming.begin());
  self.expecting_wave = false;
  self.Capture(std::span(self.incoming).first(self.wave_bytes));
}
auto SoundProtocol::Train(SoundClient& self) -> void {
  Expects(self.incoming.size() >= 8, "training request has its header");
  auto const answered = self.Send(std::span(self.incoming).first(8));
  Expects(answered, "training response is sent");
}
auto SoundProtocol::Dispatch(SoundClient& self, wStream* stream, std::uint8_t type, std::uint16_t size) -> void {
  switch (type) {
  case 7: Formats(self, stream); break;
  case 2:
  case 13: Wave(self, stream, size, type == 13); break;
  case 6:  Train(self); break;
  case 1:
  case 3:  break;
  default: utilities::Unreachable(type);
  }
}
}
