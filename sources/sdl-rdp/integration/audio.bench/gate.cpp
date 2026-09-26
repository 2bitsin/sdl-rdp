#include <sdl-rdp/headless-client.test/audio/gate.hpp>

#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/headless-client.test/audio/tone-measurements.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/waits.hpp>
#include <sdl-rdp/headless-client.test/graphics/observer.hpp>
#include <sdl-rdp/integration/support.bench/session.hpp>
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/stopwatch.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <vector>

namespace sdl_rdp::integration::audio_bench::detail::gate {
using namespace std::chrono_literals;
using sdl_rdp::configuration::Codec;
using sdl_rdp::headless_client_test::audio::AudioGate;
using sdl_rdp::headless_client_test::audio::MaximumGapMs;
using sdl_rdp::headless_client_test::audio::WriteFrames;
using sdl_rdp::headless_client_test::backend::BackendInstance;
using sdl_rdp::headless_client_test::backend::UntilLogged;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::client::SoundClient;
using sdl_rdp::headless_client_test::frame::MovingTilePattern;
using sdl_rdp::headless_client_test::graphics::GraphicsObserver;
using sdl_rdp::integration::support_bench::Check;
using sdl_rdp::integration::support_bench::Measured;
using sdl_rdp::integration::support_bench::OneSession;
using sdl_rdp::integration::support_bench::Session;
using sdl_rdp::utilities::DeadlineAfter;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::Throughout;
using sdl_rdp::utilities::Timed;
namespace {
auto ProduceProgressiveFrames(BackendInstance const& backend) -> std::size_t {
  Pixels      pixels(1280uz * 800);
  Rect const  full      { .x = 0, .y = 0, .w = 1280, .h = 800 };
  std::size_t presented = 0;
  Throughout(DeadlineAfter(2s), [&] {
    if (!backend.WaitFrame(std::chrono::milliseconds{ 10 })) return true;
    MovingTilePattern(pixels, 1280, 800, presented);
    backend.Present(pixels, 1280, 800, full);
    ++presented;
    return true;
  });
  return presented;
}
}

class AudioPlaybackConfirmsKeepRealtimeStreamContinuous final : public Session<AudioGate> {
public:
  using Session::Session;
  auto TestBody() -> void override;
};
class AudioContinuousUnderProgressiveLoad final : public Session<AudioGate> {
public:
  using Session::Session;
  auto TestBody() -> void override;

private:
  auto ThenProgressiveLoad(Client& client, SoundClient& audio, GraphicsObserver const& observer) -> void;
};
class AudioNeverConfirmsUsesServerClock final : public Session<AudioGate> {
public:
  using Session::Session;
  auto TestBody() -> void override;

private:
  auto WriteCaptured(std::vector<std::int16_t> const& pcm) -> int;
};
BENCHMARK(Measured<AudioPlaybackConfirmsKeepRealtimeStreamContinuous>)->Apply(OneSession);
BENCHMARK(Measured<AudioContinuousUnderProgressiveLoad>)->Apply(OneSession);
BENCHMARK(Measured<AudioNeverConfirmsUsesServerClock>)->Apply(OneSession);

auto AudioPlaybackConfirmsKeepRealtimeStreamContinuous::TestBody() -> void {
  if (!Passes([this] { GivenUnconfirmedSession(); }, [this] { RunRealtimeAudio(ClientSession(), AudioSession()); }))
    return;
  Record("maximum_block_gap_ms", MaximumGapMs(AudioSession().CaptureState().received));
  ClientSession().Disconnect();
  backend.Close();
  Passes([this] { CheckAudioStatistics(AudioSession()); });
}

auto AudioContinuousUnderProgressiveLoad::TestBody() -> void {
  if (!Passes([this] { Open(1280, 800, { }, Codec::Progressive); })) return;
  if (!Passes([this] { (*backend).Audio().Open(); })) return;
  auto [client, audio] = NewSession(1280, 800);
  client.EnableGraphics();
  GraphicsObserver observer(client);
  if (!Passes([&] { GivenUnconfirmedAudio(client, audio); })) return;
  if (!Check(UntilLogged(client, logs, "GFX confirmed"), "the client confirms GFX")) return;
  // Pixel decoding on the client pump thread would delay audio reception independently of server encoding.
  observer.Observed().decode = false;
  ThenProgressiveLoad(client, audio, observer);
}
auto AudioContinuousUnderProgressiveLoad::ThenProgressiveLoad(Client& client, SoundClient& audio,
                                                              GraphicsObserver const& observer) -> void {
  auto presenting = std::async(std::launch::async, [this] { return ProduceProgressiveFrames(backend); });
  if (!Passes([&] { RunRealtimeAudio(client, audio); })) return;
  Record("maximum_block_gap_ms", MaximumGapMs(audio.CaptureState().received));
  Check(presenting.get() >= 10, "the backend presents under audio load");
  Check(observer.Observed().frames.size() >= 10, "the client observes the progressive frames");
  Check(observer.Observed().progressive_headers > 0, "the payload uses the progressive codec");
  client.Disconnect();
  backend.Close();
  Passes([&] { CheckAudioStatistics(audio); });
}

auto AudioNeverConfirmsUsesServerClock::TestBody() -> void {
  if (!Passes([this] { GivenUnconfirmedSession(); })) return;
  std::vector<std::int16_t> const pcm(48000uz * 2, 1234);
  int                             written = 0;
  auto const                      span    = Timed([&] { written = WriteCaptured(pcm); });
  Measure(span);
  Check(written == 48000, "the writer returns every frame");
  Check(span >= 900ms, "the server clock paces the second");
  Check(logs.Contains("500"), "the gate reports its 500 ms fallback");
  Record("never_confirms_one_second_elapsed", std::chrono::duration<double>(span).count());
}
auto AudioNeverConfirmsUsesServerClock::WriteCaptured(std::vector<std::int16_t> const& pcm) -> int {
  auto writing = std::async(std::launch::async, [&] { return WriteFrames(*backend, pcm, 0, 48000); });
  Check(UntilCaptured(pcm.size()), "the client captures the second");
  return writing.get();
}
}
