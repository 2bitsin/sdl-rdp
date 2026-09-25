#include <sdl-rdp/headless-client.test/audio/gate.hpp>
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/audio/tone-measurements.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/graphics/observer.hpp>
#include <sdl-rdp/integration/support.bench/session.hpp>
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/stopwatch.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <vector>

namespace sdl_rdp::integration::audio_bench::detail::gate {
using sdl_rdp::headless_client_test::audio::AudioGate;
using sdl_rdp::headless_client_test::audio::MaximumGapMs;
using sdl_rdp::headless_client_test::backend::BackendInstance;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::SoundClient;
using sdl_rdp::headless_client_test::frame::MovingTilePattern;
using sdl_rdp::headless_client_test::graphics::GraphicsObserver;
using sdl_rdp::integration::support_bench::Check;
using sdl_rdp::integration::support_bench::Measured;
using sdl_rdp::integration::support_bench::OneSession;
using sdl_rdp::integration::support_bench::Session;
using sdl_rdp::utilities::DeadlineAfter;
using sdl_rdp::utilities::Throughout;
using sdl_rdp::utilities::Timed;
using namespace std::chrono_literals;
namespace {
auto ProduceProgressiveFrames(BackendInstance const& backend) -> std::size_t {
  std::vector<std::uint32_t> pixels(1280uz * 800);
  sdlrdp_rect const          full      { 0, 0, 1280, 800 };
  std::size_t                presented = 0;
  Throughout(DeadlineAfter(2s), [&] {
    if (!sdlrdp_wait_frame(backend.Handle(), 10)) return true;
    MovingTilePattern(pixels, 1280, 800, presented);
    if (backend.Present(pixels, 1280, 800, full) != 0) return false;
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
  if (!Holds([this] { GivenUnconfirmedSession(); }, [this] { RunRealtimeAudio(ClientSession(), AudioSession()); }))
    return;
  Record("maximum_block_gap_ms", MaximumGapMs(AudioSession().CaptureState().received));
  ClientSession().Disconnect();
  backend.Close();
  Holds([this] { CheckAudioStatistics(AudioSession()); });
}

auto AudioContinuousUnderProgressiveLoad::TestBody() -> void {
  if (!Holds([this] { Open(1280, 800, { }, SDLRDP_CODEC_PROGRESSIVE); })) return;
  if (!Check(sdlrdp_audio_open(backend.Handle()) == 0, "the backend opens audio")) return;
  auto [client, audio] = NewSession(1280, 800);
  client.EnableGraphics();
  GraphicsObserver observer(client);
  if (!Holds([&] { GivenUnconfirmedAudio(client, audio); })) return;
  if (!Check(client.Until([this] { return logs.Contains("GFX confirmed"); }), "the client confirms GFX")) return;
  // Pixel decoding on the client pump thread would delay audio reception independently of server encoding.
  observer.Observed().decode = false;
  ThenProgressiveLoad(client, audio, observer);
}
auto AudioContinuousUnderProgressiveLoad::ThenProgressiveLoad(Client& client, SoundClient& audio,
                                                              GraphicsObserver const& observer) -> void {
  auto presenting = std::async(std::launch::async, [this] { return ProduceProgressiveFrames(backend); });
  if (!Holds([&] { RunRealtimeAudio(client, audio); })) return;
  Record("maximum_block_gap_ms", MaximumGapMs(audio.CaptureState().received));
  Check(presenting.get() >= 10, "the backend presents under audio load");
  Check(observer.Observed().frames.size() >= 10, "the client observes the progressive frames");
  Check(observer.Observed().progressive_headers > 0, "the payload uses the progressive codec");
  client.Disconnect();
  backend.Close();
  Holds([&] { CheckAudioStatistics(audio); });
}

auto AudioNeverConfirmsUsesServerClock::TestBody() -> void {
  if (!Holds([this] { GivenUnconfirmedSession(); })) return;
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
  auto writing = std::async(std::launch::async,
                            [&] { return sdlrdp_audio_write(backend.Handle(), pcm.data(), 48000); });
  Check(UntilCaptured(pcm.size()), "the client captures the second");
  return writing.get();
}
}
