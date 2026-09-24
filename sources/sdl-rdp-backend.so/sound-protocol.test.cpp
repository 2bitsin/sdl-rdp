#include "_detail/sound-protocol.hpp"

#include "_detail/sound-client.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <freerdp/svc.h>
#include <iterator>
#include <oxbox/utilities/serdes.hpp>
#include <span>

namespace Headless {
namespace {
auto ReadSoundFormats(wStream* stream, SoundCapture& capture) -> void {
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
auto SoundFormatReply(SoundCapture const& capture) -> std::vector<BYTE> {
  auto              supported = SupportedSoundFormats(capture);
  std::vector<BYTE> bytes(24 + (supported.size() * 18));
  wStream           output    { };
  auto*             out       = Stream_StaticInit(&output, bytes.data(), bytes.size());
  WriteSoundFormatHeader(out, capture, supported.size(), bytes.size());
  std::ranges::for_each(supported, [&](auto const& format) {
    Expects(audio_format_write(out, &format), "supported PCM format serialized");
  });
  return bytes;
}
}

auto SoundProtocol::Register(CHANNEL_ENTRY_POINTS_EX* points, void* handle) -> BOOL {
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
auto SoundProtocol::Initialized(void* user, void* /*unused*/, UINT event, void* /*unused*/, UINT /*unused*/) -> void {
  auto& self = *static_cast<SoundClient*>(user);
  if (event != CHANNEL_EVENT_CONNECTED) return;
  auto name = std::to_array("rdpsnd");
  Expects(self.entry.pVirtualChannelOpenEx(self.init, &self.channel, name.data(), Received) == CHANNEL_RC_OK,
          "sound static channel opens");
  self.capture.opened = true;
}
auto SoundProtocol::Received(void* user, DWORD /*unused*/, UINT event, void* data, UINT32 size, UINT32 total,
                             UINT32 flags) -> void {
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
auto SoundProtocol::Formats(SoundClient& self, wStream* stream) -> void {
  ReadSoundFormats(stream, self.capture);
  Expects(self.Send(SoundFormatReply(self.capture)), "client format intersection sent");
  if (self.capture.version >= 6)
    Expects(self.Send(std::array<BYTE, 8>{ 12, 0, 4, 0, 2, 0, 0, 0 }), "quality mode sent");
  self.capture.ready = true;
}
auto SoundProtocol::Wave(SoundClient& self, wStream* stream, unsigned size, bool second) -> void {
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
    self.Capture({ static_cast<uint8_t const*>(Stream_Pointer(stream)), size - 12 });
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
  wStream storage { };
  auto*   stream  = Stream_StaticInit(&storage, self.incoming.data(), self.incoming.size());
  BYTE    type    = 0;
  UINT16  size    = 0;
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
auto SoundProtocol::Dispatch(SoundClient& self, wStream* stream, BYTE type, UINT16 size) -> void {
  switch (type) {
  case 7:
    Formats(self, stream);
    break;
  case 2:
  case 13:
    Wave(self, stream, size, type == 13);
    break;
  case 6:
    Expects(self.incoming.size() >= 8, "training request has its header");
    Expects(self.Send(std::span(self.incoming).first(8)), "training response is sent");
    break;
  case 1:
  case 3:
    break;
  default:
    utilities::Unreachable(type);
  }
}
}
