#include <sdl-rdp/freerdp-facade/sound-channel.hpp>

#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/handled.hpp>
#include <sdl-rdp/freerdp-facade/record-array.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>

#include <freerdp/server/rdpsnd.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>

namespace sdl_rdp::freerdp_facade::detail::sound_channel {
using sdl_rdp::utilities::AllocationFailed;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::OperationName;

static_assert(WavePcm == WAVE_FORMAT_PCM);
static_assert(SoundChannelName == RDPSND_CHANNEL_NAME);
auto ReleaseSound(s_rdpsnd_server_context* context) noexcept -> void {
  rdpsnd_server_context_free(context);
}
namespace {
constexpr OperationName AudioActivation  { "Audio activation"         };
constexpr OperationName AudioConfirmation{ "Audio block confirmation" };
auto Owner(RdpsndServerContext const& context) -> SoundChannel& {
  return CallbackOwner<SoundChannel, &RdpsndServerContext::data>(context);
}
auto ServerFormat(AudioFormat const& format) -> AUDIO_FORMAT {
  auto const frame = Narrowed<std::uint16_t>(format.channels * format.bits / 8);
  return { .wFormatTag      = format.tag,
           .nChannels       = format.channels,
           .nSamplesPerSec  = format.rate,
           .nAvgBytesPerSec = format.rate * frame,
           .nBlockAlign     = frame,
           .wBitsPerSample  = format.bits,
           .cbSize          = 0,
           .data            = nullptr };
}
auto ClientFormat(AUDIO_FORMAT const& format) -> AudioFormat {
  return {
    .tag = format.wFormatTag, .channels = format.nChannels, .rate = format.nSamplesPerSec, .bits = format.wBitsPerSample
  };
}
auto ClientFormats(RdpsndServerContext const& context) -> std::span<AUDIO_FORMAT const> {
  return RecordArray<&RdpsndServerContext::client_formats, &RdpsndServerContext::num_client_formats>(context);
}
auto Offer(RdpsndServerContext& sound, std::span<AudioFormat const> offered) -> void {
  Expects(!offered.empty(), "the server offers a format");
  sound.server_formats = audio_formats_new(offered.size());
  if (!sound.server_formats) throw AllocationFailed{ "Audio format" };
  sound.num_server_formats = offered.size();
  std::ranges::transform(offered, sound.server_formats, ServerFormat);
  // The first offered format is the source the server's samples are in.
  sound.src_format = &sound.server_formats[0];
}
}
class SoundChannel::Slots {
public:
  static auto Install(RdpsndServerContext& context) -> void;

private:
  static auto Activated(SoundChannel& channel)                                              -> void;
  static auto Confirmed(SoundChannel& channel, std::uint8_t block, std::uint16_t timestamp) -> std::uint32_t;
};
auto SoundChannel::Slots::Install(RdpsndServerContext& context) -> void {
  constexpr auto failures = [](SoundChannel const& channel, OperationName operation) noexcept {
    return SinkFailures(channel._events, operation);
  };
  // abi: psRdpsndServerActivated; psRdpsndServerConfirmBlock, BYTE is uint8_t, UINT16 is uint16_t, UINT is uint32_t
  context.Activated    = Handled<Owner, &Slots::Activated, AudioActivation, failures>;
  context.ConfirmBlock = Handled<Owner, &Slots::Confirmed, AudioConfirmation, failures, ERROR_INTERNAL_ERROR>;
}
auto SoundChannel::Slots::Activated(SoundChannel& channel) -> void {
  channel._events.Activated(channel.Client());
}
auto SoundChannel::Slots::Confirmed(SoundChannel& channel, std::uint8_t block, std::uint16_t timestamp)
    -> std::uint32_t {
  channel._events.Confirmed({ .block = block, .timestamp = timestamp });
  return CHANNEL_RC_OK;
}

SoundChannel::SoundChannel(ChannelManager& channels, Connection& connection, SoundChannelEvents& events,
                           std::span<AudioFormat const> offered, std::chrono::milliseconds latency)
    : _channels{ channels }, _events{ events }, _context{ channels.Create<SoundContext, rdpsnd_server_context_new>() } {
  if (!_context) throw AllocationFailed{ "Audio channel" };
  auto& context = *_context;
  context.data = this;
  context.rdpcontext = &connection.Context();
  context.use_dynamic_virtual_channel = false;
  context.latency = Narrowed<std::uint32_t>(latency.count());
  Offer(context, offered);
  Slots::Install(context);
}
SoundChannel::~SoundChannel() {
  _context.reset();
  _channels.Reclaim(SoundChannelName);
}
auto SoundChannel::Initialize() -> bool {
  auto& sound = Context();
  return sound.Initialize(&sound, false) == CHANNEL_RC_OK;
}
auto SoundChannel::Pump() -> SoundPump {
  auto& sound = Context();
  return Pumped(rdpsnd_server_handle_messages(&sound), sound.num_client_formats != 0);
}
auto SoundChannel::Pumped(std::uint32_t result, bool answered) -> SoundPump {
  switch (result) {
  case CHANNEL_RC_OK:
  case ERROR_NO_DATA:        return SoundPump::Handled;
  case ERROR_INTERNAL_ERROR: return answered ? SoundPump::Failed : SoundPump::FailedBeforeFormats;
  default:                   return SoundPump::Failed;
  }
}
auto SoundChannel::Handle() const -> WaitHandle {
  return WaitHandle::Lent<rdpsnd_server_get_event_handle>(Context());
}
auto SoundChannel::Client() const -> SoundClient {
  auto const& sound  = Context();
  SoundClient client { .version = sound.clientVersion };
  std::ranges::transform(ClientFormats(sound), std::back_inserter(client.formats), ClientFormat);
  return client;
}
auto SoundChannel::Volume() const -> std::optional<std::uint32_t> {
  auto const& sound = Context();
  if (!(sound.capsFlags & TSSNDCAPS_VOLUME)) return std::nullopt;
  return sound.initialVolume;
}
auto SoundChannel::NextBlock() const -> std::uint8_t {
  return Context().block_no;
}
auto SoundChannel::Select(std::size_t index) -> AudioFormat {
  auto&      sound   = Context();
  auto const formats = ClientFormats(sound);
  Expects(index < formats.size(), "client format exists");
  sound.selected_client_format = Narrowed<std::uint16_t>(index);
  return ClientFormat(formats[index]);
}
auto SoundChannel::SendSamples(std::span<std::int16_t const> samples, std::uint16_t timestamp) -> bool {
  auto& sound = Context();
  return sound.SendSamples2(&sound, sound.selected_client_format, samples.data(), samples.size_bytes(), timestamp, 0)
         == CHANNEL_RC_OK;
}
auto SoundChannel::Context() const -> s_rdpsnd_server_context& {
  Expects(_context != nullptr, "the sound context exists");
  return *_context;
}
}
