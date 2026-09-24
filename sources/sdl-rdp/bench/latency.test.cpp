#include <sdl-rdp/sample-gate.test/process.hpp>
#include <sdl-rdp/sample-gate.test/sample-launch.hpp>
#include <sdl-rdp/sample-gate.test/sample.hpp>
#include <sdl-rdp/sample-gate.test/trace-number.hpp>
#include <sdl-rdp/sample-gate.test/wall-milliseconds.hpp>

#include <SDL3/SDL.h>
#include <sdl-rdp/headless-client.test/clipboard-client.hpp>
#include <sdl-rdp/headless-client.test/frame-observer.hpp>
#include <sdl-rdp/headless-client.test/sound-client.hpp>
#include <algorithm>
#include <filesystem>
#include <format>
#include <ranges>
#include <span>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace SampleGate {
namespace {
auto RecordSample(std::string_view line, std::string_view prefix, std::span<int64_t const> sent,
                  std::vector<int64_t>& latency) -> void {
  ASSERT_LT(latency.size(), sent.size());
  auto elapsed = TraceNumber(line, prefix) - sent[latency.size()];
  EXPECT_GE(elapsed, 0);
  latency.push_back(elapsed);
}
auto SendTime(Client const& client) -> int64_t {
  Expects(client.Instance() != nullptr, "latency client exists");
  return WallMilliseconds();
}

auto ReportLatency(std::vector<int64_t>& latency, std::string_view event) -> void {
  std::ranges::sort(latency);
  auto p95 = latency[((latency.size() * 95 + 99) / 100) - 1];
  testing::Test::RecordProperty(std::string(event) + "_p95_ms", p95);
  SDL_Log("latency %.*s count=%zu p95=%lld ms", int(event.size()), event.data(), latency.size(),
          static_cast<long long>(p95));
  EXPECT_LT(p95, 40) << event;
}
auto CheckLatency(std::string const& trace, std::string_view event, std::span<int64_t const> sent) -> void {
  Expects(!event.empty(), "trace event is named");
  Expects(!sent.empty(), "client sent measured events");
  auto                 prefix  = std::format("trace {} t=", event);
  std::vector<int64_t> latency;
  std::istringstream   lines(trace);
  for (std::string line; std::getline(lines, line);) {
    if (!line.contains(prefix)) continue;
    if (event == "key" && !line.contains(" code=30 ")) continue;
    RecordSample(line, prefix, sent, latency);
    if (::testing::Test::HasFatalFailure()) return;
  }
  ASSERT_EQ(latency.size(), sent.size()) << event;
  ReportLatency(latency, event);
}
}

namespace {
auto PumpUntil(Client& client, Headless::FrameObserver& frames, Clock::time_point deadline) -> void {
  while (Clock::now() < deadline) {
    ASSERT_TRUE(client.Pump(1));
    if (!frames.Frames().empty()) ASSERT_TRUE(frames.Ack());
  }
}
auto SampleLatency(Client& client, Headless::FrameObserver& frames, Headless::ClipboardClient& clipboard,
                   std::vector<int64_t>& keys, std::vector<int64_t>& clips, Clock::time_point start) -> void {
  for (unsigned i = 0; i < 60; ++i) {
    PumpUntil(client, frames, start + i * 50ms);
    if (::testing::Test::HasFatalFailure()) return;
    keys.push_back(SendTime(client));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input,
                                                  i % 2 ? KBD_FLAGS_RELEASE : KBD_FLAGS_DOWN, 0x1e));
    clips.push_back(SendTime(client));
    ASSERT_EQ(clipboard.Offer({ BYTE('A' + i), 0, 0, 0 }), CHANNEL_RC_OK);
  }
  PumpUntil(client, frames, start + 3s);
}
}
namespace {
auto TightAudioArguments(fs::path const& certificates) -> std::vector<std::string> {
  auto arguments = Arguments(certificates, false);
  arguments.insert(arguments.begin() + 1, { "SDL_AUDIO_DRIVER=rdp", "SDL_RDP_TRACE=1", "SDL_LOGGING=video=info" });
  arguments.insert(arguments.end(), { "--tone", "--tight" });
  return arguments;
}
}
namespace {
auto DrainTrace(Process& process, std::stop_token const& stop) -> void {
  std::string output;
  while (!stop.stop_requested()) process.Line(output, Clock::now() + 10ms);
}
}
namespace {
auto ThenMediaReady(Client& client, Headless::FrameObserver& frames, Headless::SoundClient& audio,
                    Headless::ClipboardClient& clipboard) -> void {
  ASSERT_TRUE(client.Until([&] {
    if (!frames.Frames().empty()) frames.Ack();
    return !audio.CaptureState().received.empty() && clipboard.Observed().accepted.load() > 0;
  }));
}
}
namespace {
auto FinishLatency(std::jthread& drain, Process const& process, std::span<int64_t const> keys,
                   std::span<int64_t const> clips) -> void {
  drain.request_stop();
  drain.join();
  CheckLatency(process.Transcript(), "key", keys);
  CheckLatency(process.Transcript(), "clipboard", clips);
}
}
TEST_F(Sample, InputAndClipboardUnderTightVideo) {
  Expects(process == nullptr, "sample has not started");
  GivenAudioProcess(TightAudioArguments(certificates.Path()));
  if (::testing::Test::HasFatalFailure()) return;
  std::jthread              drain([&](std::stop_token const& stop) { DrainTrace(*process, stop); });
  Client                    client(audio_port, true, 640, 480);
  Headless::ClipboardClient clipboard(client);
  Headless::SoundClient     audio(client);
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  Headless::FrameObserver frames(client);
  ThenMediaReady(client, frames, audio, clipboard);
  if (::testing::Test::HasFatalFailure()) return;
  std::vector<int64_t> keys;
  std::vector<int64_t> clips;
  auto                 start  = Clock::now();
  auto                 before = frames.Frames().size();
  SampleLatency(client, frames, clipboard, keys, clips, start);
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_GE(frames.Frames().size() - before, 60u);
  Escape(client);
  if (::testing::Test::HasFatalFailure()) return;
  FinishLatency(drain, *process, keys, clips);
}
}
