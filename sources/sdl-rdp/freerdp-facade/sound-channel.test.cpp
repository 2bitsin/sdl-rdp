#include <sdl-rdp/freerdp-facade/sound-channel.hpp>

#include <sdl-rdp/freerdp-facade/support.test/recorded-failures.hpp>
#include <sdl-rdp/freerdp-facade/support.test/unjoined-connection.hpp>

#include <freerdp/server/rdpsnd.h>
#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::sound_channel {
class SoundChannelProbe {
public:
  static auto Context(SoundChannel const& channel) -> RdpsndServerContext& {
    return channel.Context();
  }
  static auto Pumped(std::uint32_t result, bool answered) -> SoundPump {
    return SoundChannel::Pumped(result, answered);
  }
};
namespace {
using sdl_rdp::freerdp_facade::support_test::RecordedFailures;
using sdl_rdp::freerdp_facade::support_test::UnjoinedConnection;

struct Recorded {
  bool                      throws  { };
  std::vector<SoundClient>  clients;
  std::vector<BlockConfirm> confirms;
  std::vector<std::string>  failures;
};
class Recorder final : public RecordedFailures<SoundChannelEvents> {
public:
  explicit Recorder(Recorded& recorded) : RecordedFailures{ recorded.failures }, _recorded{ recorded } { }
  auto     Activated(SoundClient const& client) -> void override {
    if (_recorded.throws) throw std::runtime_error{ "refused" };
    _recorded.clients.push_back(client);
  }
  auto Confirmed(BlockConfirm confirm) -> void override {
    if (_recorded.throws) throw std::runtime_error{ "refused" };
    _recorded.confirms.push_back(confirm);
  }

private:
  Recorded& _recorded;
};
constexpr std::array Offered{ AudioFormat{ .tag = WavePcm, .channels = 2, .rate = 44100, .bits = 16 },
                              AudioFormat{ .tag = WavePcm, .channels = 2, .rate = 48000, .bits = 16 } };
class SoundSlots : public testing::Test {
protected:
  auto Answered(std::uint16_t version, std::vector<AUDIO_FORMAT> const& formats) -> void {
    context.clientVersion      = version;
    context.client_formats     = audio_formats_new(formats.size());
    context.num_client_formats = static_cast<std::uint16_t>(formats.size());
    std::ranges::copy(formats, context.client_formats);
  }
  Recorded             recorded;
  Recorder             events  { recorded                            };
  UnjoinedConnection   unjoined;
  SoundChannel channel{ unjoined.channels, unjoined.connection, events, Offered, std::chrono::milliseconds{ 10 } };
  RdpsndServerContext& context { SoundChannelProbe::Context(channel) };
};
auto Pcm(std::uint32_t rate, std::uint16_t channels, std::uint16_t bits = 16) -> AUDIO_FORMAT {
  AUDIO_FORMAT format{ };
  format.wFormatTag     = WAVE_FORMAT_PCM;
  format.nChannels      = channels;
  format.nSamplesPerSec = rate;
  format.wBitsPerSample = bits;
  return format;
}
}
TEST_F(SoundSlots, TheOfferIsTheServerFormatsWithTheFirstAsSource) {
  ASSERT_EQ(context.num_server_formats, 2U);
  EXPECT_EQ(context.server_formats[0].nSamplesPerSec, 44100U);
  EXPECT_EQ(context.server_formats[0].nAvgBytesPerSec, 44100U * 4);
  EXPECT_EQ(context.server_formats[0].nBlockAlign, 4);
  EXPECT_EQ(context.server_formats[1].nSamplesPerSec, 48000U);
  EXPECT_EQ(context.src_format, &context.server_formats[0]);
  EXPECT_EQ(context.latency, 10U);
  EXPECT_FALSE(context.use_dynamic_virtual_channel);
}
TEST_F(SoundSlots, ActivationCarriesTheClientVersionAndFormats) {
  Answered(8, { Pcm(48000, 2), Pcm(22050, 1, 8) });
  context.Activated(&context);
  ASSERT_EQ(recorded.clients.size(), 1U);
  EXPECT_EQ(recorded.clients[0].version, 8);
  ASSERT_EQ(recorded.clients[0].formats.size(), 2U);
  EXPECT_EQ(recorded.clients[0].formats[0].rate, 48000U);
  EXPECT_EQ(recorded.clients[0].formats[0].bits, 16);
  EXPECT_EQ(recorded.clients[0].formats[1].channels, 1);
  EXPECT_EQ(recorded.clients[0].formats[1].bits, 8);
  EXPECT_EQ(recorded.clients[0].formats[1].tag, WavePcm);
}
TEST_F(SoundSlots, AThrowingActivationIsReported) {
  recorded.throws = true;
  context.Activated(&context);
  EXPECT_EQ(recorded.failures, std::vector<std::string>{ "Audio activation" });
}
TEST_F(SoundSlots, AConfirmationCarriesItsBlockAndTimestamp) {
  EXPECT_EQ(context.ConfirmBlock(&context, 3, 1234), CHANNEL_RC_OK);
  ASSERT_EQ(recorded.confirms.size(), 1U);
  EXPECT_EQ(recorded.confirms[0].block, 3);
  EXPECT_EQ(recorded.confirms[0].timestamp, 1234);
}
TEST_F(SoundSlots, AThrowingConfirmationIsAnInternalError) {
  recorded.throws = true;
  EXPECT_EQ(context.ConfirmBlock(&context, 3, 1234), ERROR_INTERNAL_ERROR);
  EXPECT_EQ(recorded.failures, std::vector<std::string>{ "Audio block confirmation" });
}
TEST_F(SoundSlots, SelectingAFormatNamesItForTheSamples) {
  Answered(8, { Pcm(44100, 2), Pcm(48000, 2) });
  EXPECT_EQ(channel.Select(1).rate, 48000U);
  EXPECT_EQ(context.selected_client_format, 1);
  EXPECT_DEATH(std::ignore = channel.Select(2), "client format exists");
}
TEST_F(SoundSlots, TheVolumeIsTheClientsOnlyWhenItHasTheCapability) {
  context.initialVolume = 0x80004000;
  EXPECT_EQ(channel.Volume(), std::nullopt);
  context.capsFlags = TSSNDCAPS_VOLUME;
  EXPECT_EQ(channel.Volume(), 0x80004000U);
}
TEST(SoundPumps, AnInternalErrorBeforeAnyClientFormatIsItsOwnState) {
  EXPECT_EQ(SoundChannelProbe::Pumped(ERROR_INTERNAL_ERROR, false), SoundPump::FailedBeforeFormats);
  EXPECT_EQ(SoundChannelProbe::Pumped(ERROR_INTERNAL_ERROR, true), SoundPump::Failed);
}
TEST(SoundPumps, NoDataIsHandledAndAnyOtherErrorFails) {
  EXPECT_EQ(SoundChannelProbe::Pumped(CHANNEL_RC_OK, false), SoundPump::Handled);
  EXPECT_EQ(SoundChannelProbe::Pumped(ERROR_NO_DATA, false), SoundPump::Handled);
  EXPECT_EQ(SoundChannelProbe::Pumped(ERROR_INVALID_DATA, false), SoundPump::Failed);
}
}
