#include <sdl-rdp/headless-client.test/client/sound-protocol.hpp>

#include <sdl-rdp/headless-client.test/client/sound.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/svc.h>
#include <oxbox/utilities/serdes.hpp>
#include <oxbox/utilities/span.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <span>

namespace sdl_rdp::headless_client_test::client::detail::sound_protocol {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Unreachable;

namespace {
// The MS-RDPEA Quality Mode PDU: its header, then HIGH_QUALITY and the padding.
constexpr std::array<std::uint8_t, 8> QualityMode{ 12, 0, 4, 0, 2, 0, 0, 0 };
auto ReadSoundFormats(wStream& stream, SoundCapture& capture) -> void {
  auto const remaining = Stream_GetRemainingLength(&stream);
  Expects(remaining >= 20, "server format header complete");
  Stream_Seek(&stream, 14);
  std::uint16_t count = 0;
  Stream_Read_UINT16(&stream, count);
  Stream_Seek(&stream, 4);
  capture.server_formats.resize(count);
  std::ranges::for_each(capture.server_formats, [&](auto& format) {
    auto const read = audio_format_read(&stream, &format);
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
auto WriteSoundFormatHeader(wStream& out, SoundCapture const& capture, std::size_t count, std::size_t size) -> void {
  auto const capacity = Stream_GetRemainingCapacity(&out);
  Expects(capacity >= 24, "format reply header fits");
  oxbox::utilities::BoundedWriter writer(
      std::as_writable_bytes(std::span(static_cast<std::byte*>(Stream_Pointer(&out)), 24)));
  writer.Store<std::uint8_t>(7);
  writer.Store<std::uint8_t>(0);
  writer.Store<std::uint16_t, std::endian::little>(Narrowed<std::uint16_t>(size - 4));
  writer.Store<std::uint32_t, std::endian::little>(3);
  writer.Store<std::uint32_t, std::endian::little>(capture.volume);
  writer.Store<std::uint32_t, std::endian::little>(0);
  writer.Store<std::uint16_t, std::endian::little>(0);
  writer.Store<std::uint16_t, std::endian::little>(Narrowed<std::uint16_t>(count));
  writer.Store<std::uint8_t>(0);
  writer.Store<std::uint16_t, std::endian::little>(capture.version);
  writer.Store<std::uint8_t>(0);
  auto const filled = writer.Whole();
  Expects(filled, "sound format header fills its wire layout");
  Stream_Seek(&out, 24);
}
auto SoundFormatReply(SoundCapture const& capture) -> std::vector<std::byte> {
  auto                   supported = SupportedSoundFormats(capture);
  std::vector<std::byte> bytes(24 + (supported.size() * 18));
  auto const             wire      = oxbox::utilities::SpanCast<std::uint8_t>(std::span(bytes));
  wStream                output    { };
  auto&                  out       = *Stream_StaticInit(&output, wire.data(), wire.size());
  WriteSoundFormatHeader(out, capture, supported.size(), bytes.size());
  std::ranges::for_each(supported, [&](auto const& format) {
    auto const written = audio_format_write(&out, &format);
    Expects(written, "supported PCM format serialized");
  });
  return bytes;
}
}

auto SoundProtocol::Entry(CHANNEL_ENTRY_POINTS_EX* points, void* handle) -> int {
  Expects(points != nullptr, "channel entry points supplied");
  // C ABI: FreeRDP hands the extended entry points through the base pointer.
  auto const& extended = *reinterpret_cast<CHANNEL_ENTRY_POINTS_FREERDP_EX*>(points);
  Expects(extended.pExtendedData != nullptr, "capture context supplied");
  auto& self = *static_cast<SoundClient*>(extended.pExtendedData);
  self.open = points->pVirtualChannelOpenEx;
  CHANNEL_DEF definition{ };
  std::memcpy(definition.name, "rdpsnd", 7);
  definition.options = CHANNEL_OPTION_INITIALIZED | CHANNEL_OPTION_ENCRYPT_RDP;
  return points->pVirtualChannelInitEx(&self, nullptr, handle, &definition, 1, VIRTUAL_CHANNEL_VERSION_WIN2000,
                                       Initialized)
         == CHANNEL_RC_OK;
}
auto SoundProtocol::Initialized(void* user, void* init, std::uint32_t event, void* /*data*/, std::uint32_t /*size*/)
    -> void {
  Expects(user != nullptr, "the init event names its capture");
  if (event != CHANNEL_EVENT_CONNECTED) return;
  auto&      self   = *static_cast<SoundClient*>(user);
  auto       name   = std::to_array("rdpsnd");
  auto const opened = self.open(init, &self.channel, name.data(), Opened);
  Expects(opened == CHANNEL_RC_OK, "sound static channel opens");
  self.capture.opened = true;
}
auto SoundProtocol::Opened(void* user, std::uint32_t /*open*/, std::uint32_t event, void* data, std::uint32_t size,
                           std::uint32_t total, std::uint32_t flags) -> void {
  Expects(user != nullptr, "the open event names its capture");
  if (event != CHANNEL_EVENT_DATA_RECEIVED) return;
  Expects(data != nullptr, "received data is supplied");
  Received(*static_cast<SoundClient*>(user), std::span(static_cast<std::byte const*>(data), size), total, flags);
}
auto SoundProtocol::Received(SoundClient& self, std::span<std::byte const> bytes, std::size_t total,
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
auto SoundProtocol::Formats(SoundClient& self, wStream& stream) -> void {
  ReadSoundFormats(stream, self.capture);
  auto const replied = self.Send(SoundFormatReply(self.capture));
  Expects(replied, "client format intersection sent");
  if (self.capture.version >= 6) {
    auto const quality_sent = self.Send(std::as_bytes(std::span(QualityMode)));
    Expects(quality_sent, "quality mode sent");
  }
  self.capture.ready = true;
}
auto SoundProtocol::Wave(SoundClient& self, wStream& stream, std::uint32_t size, bool second) -> void {
  auto const remaining = Stream_GetRemainingLength(&stream);
  Expects(remaining >= 12, "wave header complete");
  std::uint16_t format = 0;
  Stream_Read_UINT16(&stream, self.timestamp);
  Stream_Read_UINT16(&stream, format);
  Stream_Read_UINT8(&stream, self.block);
  Stream_Seek(&stream, 3);
  Expects(format == 0, "announced PCM format selected");
  if (second) {
    Stream_Seek(&stream, 4);
    Expects(size >= 12, "wave PDU includes its fixed header");
    auto const remaining_after_header = Stream_GetRemainingLength(&stream);
    Expects(remaining_after_header >= size - 12, "wave payload fits the remaining stream");
    self.Capture({ static_cast<std::byte const*>(Stream_Pointer(&stream)), size - 12 });
  } else {
    Stream_Read(&stream, self.first.data(), self.first.size());
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
  auto const    wire    = oxbox::utilities::SpanCast<std::uint8_t>(std::span(self.incoming));
  wStream       storage { };
  auto&         stream  = *Stream_StaticInit(&storage, wire.data(), wire.size());
  std::uint8_t  type    = 0;
  std::uint16_t size    = 0;
  Stream_Read_UINT8(&stream, type);
  Stream_Seek(&stream, 1);
  Stream_Read_UINT16(&stream, size);
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
auto SoundProtocol::Dispatch(SoundClient& self, wStream& stream, std::uint8_t type, std::uint16_t size) -> void {
  switch (type) {
  case 7: Formats(self, stream); break;
  case 2:
  case 13: Wave(self, stream, size, type == 13); break;
  case 6:  Train(self); break;
  case 1:
  case 3:  break;
  default: Unreachable(type);
  }
}
}
