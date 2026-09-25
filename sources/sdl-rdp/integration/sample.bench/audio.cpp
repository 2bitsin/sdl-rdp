#include <sdl-rdp/headless-client.test/audio/tone-measurements.hpp>
#include <sdl-rdp/headless-client.test/client/sound.hpp>
#include <sdl-rdp/headless-client.test/frame/observer.hpp>
#include <sdl-rdp/integration/support.bench/session.hpp>
#include <sdl-rdp/sample-gate.test/audio/driver.hpp>
#include <sdl-rdp/sample-gate.test/audio/sample.hpp>
#include <sdl-rdp/utilities/deadline.hpp>

#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

namespace sdl_rdp::integration::sample_bench::detail::audio {
using sdl_rdp::headless_client_test::audio::MaximumGapMs;
using sdl_rdp::headless_client_test::client::Clock;
using sdl_rdp::headless_client_test::client::SoundClient;
using sdl_rdp::headless_client_test::frame::FrameObserver;
using sdl_rdp::integration::support_bench::Check;
using sdl_rdp::integration::support_bench::Fail;
using sdl_rdp::integration::support_bench::Measured;
using sdl_rdp::integration::support_bench::OneSession;
using sdl_rdp::integration::support_bench::Session;
using sdl_rdp::sample_gate_test::audio::AudioDriver;
using sdl_rdp::sample_gate_test::audio::AudioSample;
using sdl_rdp::utilities::DeadlineAfter;
using sdl_rdp::utilities::Throughout;
using namespace std::chrono_literals;
namespace {
auto ThreeSecondsCaptured(SoundClient const& audio, FrameObserver const& /*frames*/) -> bool {
  auto const& received = audio.CaptureState().received;
  return !received.empty() && Clock::now() >= received.front() + 3s;
}
auto BlocksInSecond(SoundClient const& audio, std::chrono::seconds second) -> std::ptrdiff_t {
  auto const& received = audio.CaptureState().received;
  auto const  start    = received.front() + second;
  return std::ranges::count_if(received, [&](auto time) { return time >= start && time < start + 1s; });
}
}

class BlockCadence final : public Session<AudioSample> {
public:
  using Session::Session;
  auto TestBody() -> void override;

private:
  auto ThenBlockCadence(SoundClient const& audio, bool tight) -> void;
};
class DrainingSession : public Session<AudioDriver> {
public:
  using Session::Session;

protected:
  auto DrainedSpan(std::span<std::int16_t const> pcm, std::chrono::seconds timeout, std::chrono::milliseconds poll)
      -> std::optional<Clock::duration>;
};
class NoClientTenSecondClock final : public DrainingSession {
public:
  using DrainingSession::DrainingSession;
  auto TestBody() -> void override;

private:
  auto ThenTenSeconds(Clock::duration elapsed) -> void;
};
class InitialLeadClock final : public Session<AudioDriver> {
public:
  using Session::Session;
  auto TestBody() -> void override;
};
class LeadCadence final : public Session<AudioDriver> {
public:
  using Session::Session;
  auto TestBody() -> void override;

private:
  auto ThenLeadCadence(std::size_t first, std::size_t frames) -> void;
};
class StallRefillClock final : public Session<AudioDriver> {
public:
  using Session::Session;
  auto TestBody() -> void override;
};
class ZeroLeadKeepsRealtimeClock final : public DrainingSession {
public:
  using DrainingSession::DrainingSession;
  auto TestBody() -> void override;
};
BENCHMARK(Measured<BlockCadence>)->Apply(OneSession);
BENCHMARK(Measured<NoClientTenSecondClock>)->Apply(OneSession);
BENCHMARK(Measured<InitialLeadClock>)->Apply(OneSession);
BENCHMARK(Measured<LeadCadence>)->Apply(OneSession);
BENCHMARK(Measured<StallRefillClock>)->Apply(OneSession);
BENCHMARK(Measured<ZeroLeadKeepsRealtimeClock>)->Apply(OneSession);

// Measures from the flushed play to the drained queue whether or not it drains; nullopt when it does not.
auto DrainingSession::DrainedSpan(std::span<std::int16_t const> pcm, std::chrono::seconds timeout,
                                  std::chrono::milliseconds poll) -> std::optional<Clock::duration> {
  auto const started = PlayFlushed(pcm);
  auto const drained = QueueDrained(started + timeout, poll);
  auto const span    = Clock::now() - started;
  Measure(span);
  if (!drained) return std::nullopt;
  return span;
}

auto BlockCadence::TestBody() -> void {
  WhenTonePlayedTwice(ThreeSecondsCaptured, [this](auto const& audio, auto const& /*frames*/, bool tight) {
    ThenBlockCadence(audio, tight);
  });
}
auto BlockCadence::ThenBlockCadence(SoundClient const& audio, bool tight) -> void {
  for (auto const second : { 0s, 1s, 2s }) {
    auto const blocks = BlocksInSecond(audio, second);
    Record(std::format("{}_second_{}_blocks", tight ? "tight" : "loose", second.count()), static_cast<double>(blocks));
    Check(blocks >= 45, std::format("tight={} second={} carries 45 blocks", tight, second.count()));
  }
}

auto NoClientTenSecondClock::TestBody() -> void {
  Check(SDL_WasInit(SDL_INIT_VIDEO) == 0, "video stays uninitialised");
  auto const* driver = SDL_GetCurrentAudioDriver();
  if (driver == nullptr) {
    Fail("an audio driver is current");
    return;
  }
  Check(std::string_view{ driver } == "rdp", "the rdp audio driver is current");
  std::vector<std::int16_t> const frames(480000uz * 2, 1000);
  auto const                      span   = DrainedSpan(frames, 30s, 5ms);
  if (!span) {
    Fail("the stream drains ten seconds of PCM");
    return;
  }
  ThenTenSeconds(*span);
}
auto NoClientTenSecondClock::ThenTenSeconds(Clock::duration elapsed) -> void {
  int           buffer_frames = 0;
  SDL_AudioSpec format        { };
  if (!Check(SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream.get()), &format, &buffer_frames),
             "the device reports its format"))
    return;
  auto const buffer = std::chrono::duration<double>(static_cast<double>(buffer_frames) / format.freq);
  // Consuming ten seconds of PCM may run one lead ahead of real time.
  // SDL may dequeue one buffer ahead; scheduling delays only make this longer.
  Check(elapsed >= 10s - 150ms - buffer, "ten seconds of PCM take ten seconds");
  Record("no_client_ten_seconds_elapsed", std::chrono::duration<double>(elapsed).count());
}

auto InitialLeadClock::TestBody() -> void {
  if (!Passes([this] { ReceiveLead(); })) return;
  auto const& received = sound->CaptureState().received;
  Check(received.back() <= received.front() + 100ms, "the lead arrives within 100 ms");
}

auto LeadCadence::TestBody() -> void {
  if (!Passes([this] { ReceiveLead(); })) return;
  auto const first  = sound->CaptureState().received.size();
  auto const frames = sound->CaptureState().samples.size() / 2;
  auto const pumped = Throughout(DeadlineAfter(1s), [this] { return sound_client->Pump(1); });
  if (!Check(pumped, "the client pumps")) return;
  ThenLeadCadence(first, frames);
}
auto LeadCadence::ThenLeadCadence(std::size_t first, std::size_t frames) -> void {
  auto const& capture = sound->CaptureState();
  if (!Check(capture.received.size() > first, "blocks follow the lead")) return;
  auto const maximum_gap = MaximumGapMs(std::span(capture.received).subspan(first - 1));
  auto const sent_frames = (capture.samples.size() / 2) - frames;
  auto const block_ms = 1000.0 * static_cast<double>(sent_frames) / static_cast<double>(capture.received.size() - first)
                        / capture.rate;
  Record("maximum_block_gap_ms", maximum_gap);
  Check(maximum_gap <= (2 * block_ms) + 10, "no gap exceeds two blocks");
  auto const sent    = std::chrono::duration<double>(static_cast<double>(sent_frames) / capture.rate);
  auto const elapsed = capture.received.back() - capture.received[first - 1];
  Check(std::chrono::abs(sent - elapsed) <= 30ms, "blocks arrive in real time");
}

auto StallRefillClock::TestBody() -> void {
  RefillLead([this](SoundClient const& audio, Clock::time_point resumed) {
    Check(audio.CaptureState().received.back() <= resumed + 100ms, "the refill arrives within 100 ms");
  });
}

auto ZeroLeadKeepsRealtimeClock::TestBody() -> void {
  stream.reset();
  if (!Check(SDL_SetHint(SDL_HINT_RDP_AUDIO_LEAD, "0"), "the lead hint is set")) return;
  if (!Passes([this] { OpenStream(); })) return;
  std::vector<std::int16_t> const pcm(48000uz * 2, 1234);
  auto const                      span = DrainedSpan(pcm, 3s, 1ms);
  if (!span) {
    Fail("the stream drains one second of PCM");
    return;
  }
  Check(*span >= 990ms, "one second of PCM takes a second");
}
}
