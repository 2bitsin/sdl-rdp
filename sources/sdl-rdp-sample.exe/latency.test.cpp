#include "_detail/sample-fixture.hpp"

#include <sdl-rdp-backend.so/_detail/headless-audio.hpp>
#include <sdl-rdp-backend.so/_detail/headless-clipboard.hpp>

namespace SampleGate {
namespace {
void RecordSample(std::string const& line, std::size_t offset, std::span<int64_t const> sent,
                  std::vector<int64_t>& latency) {
  ASSERT_LT(latency.size(), sent.size());
  auto elapsed = std::stoll(line.substr(offset)) - sent[latency.size()];
  EXPECT_GE(elapsed, 0);
  latency.push_back(elapsed);
}
int64_t SendTime(Client const& client) {
  Expects(client.Instance() != nullptr, "latency client exists");
  return WallMilliseconds();
}

void ReportLatency(std::vector<int64_t>& latency, std::string_view event) {
  std::ranges::sort(latency);
  auto p95 = latency[((latency.size() * 95 + 99) / 100) - 1];
  testing::Test::RecordProperty(std::string(event) + "_p95_ms", p95);
  SDL_Log("latency %.*s count=%zu p95=%lld ms", int(event.size()), event.data(), latency.size(),
          static_cast<long long>(p95));
  EXPECT_LT(p95, 40) << event;
}
void CheckLatency(std::string const& trace, std::string_view event, std::span<int64_t const> sent) {
  Expects(!event.empty(), "trace event is named");
  Expects(!sent.empty(), "client sent measured events");
  auto prefix = std::format("trace {} t=", event);
  std::vector<int64_t> latency;
  std::istringstream lines(trace);
  for (std::string line; std::getline(lines, line);) {
    auto at = line.find(prefix);
    if (at == std::string::npos) continue;
    if (event == "key" && !line.contains(" code=30 ")) continue;
    RecordSample(line, at + prefix.size(), sent, latency);
    if (::testing::Test::HasFatalFailure()) return;
  }
  ASSERT_EQ(latency.size(), sent.size()) << event;
  ReportLatency(latency, event);
}
}

namespace {
void PumpUntil(Client& client, Headless::FrameObserver& frames, Clock::time_point deadline) {
  while (Clock::now() < deadline) {
    ASSERT_TRUE(client.Pump(1));
    if (!frames.Frames().empty()) ASSERT_TRUE(frames.Ack());
  }
}
void SampleLatency(Client& client, Headless::FrameObserver& frames, Headless::ClipboardClient& clipboard,
                   std::vector<int64_t>& keys, std::vector<int64_t>& clips, Clock::time_point start) {
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
std::vector<std::string> TightAudioArguments(fs::path const& certificates) {
  auto arguments = Arguments(certificates, false);
  arguments.insert(arguments.begin() + 1, { "SDL_AUDIO_DRIVER=rdp", "SDL_RDP_TRACE=1", "SDL_LOGGING=video=info" });
  arguments.insert(arguments.end(), { "--tone", "--tight" });
  return arguments;
}
}
namespace {
void DrainTrace(Process& process, std::stop_token const& stop) {
  std::string output;
  while (!stop.stop_requested())
    process.Line(output, Clock::now() + 10ms);
}
}
namespace {
void ThenMediaReady(Client& client, Headless::FrameObserver& frames, Headless::SoundClient& audio,
                    Headless::ClipboardClient& clipboard) {
  ASSERT_TRUE(client.Until([&] {
    if (!frames.Frames().empty()) frames.Ack();
    return !audio.CaptureState().received.empty() && clipboard.Observed().accepted.load() > 0;
  }));
}
}
namespace {
void FinishLatency(std::jthread& drain, Process const& process, std::span<int64_t const> keys,
                   std::span<int64_t const> clips) {
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
  std::jthread drain([&](std::stop_token const& stop) { DrainTrace(*process, stop); });
  Client client(audio_port, true, 640, 480);
  Headless::ClipboardClient clipboard(client);
  Headless::SoundClient audio(client);
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  Headless::FrameObserver frames(client);
  ThenMediaReady(client, frames, audio, clipboard);
  if (::testing::Test::HasFatalFailure()) return;
  std::vector<int64_t> keys;
  std::vector<int64_t> clips;
  auto start  = Clock::now();
  auto before = frames.Frames().size();
  SampleLatency(client, frames, clipboard, keys, clips, start);
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_GE(frames.Frames().size() - before, 60u);
  Escape(client);
  if (::testing::Test::HasFatalFailure()) return;
  FinishLatency(drain, *process, keys, clips);
}
}
