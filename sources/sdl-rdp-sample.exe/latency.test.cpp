#include "_detail/sample-fixture.hpp"
#include <sdl-rdp-backend.so/_detail/headless-clipboard.hpp>
#include <sdl-rdp-backend.so/_detail/headless-audio.hpp>

namespace SampleGate {
namespace {
int64_t SendTime(Client const& client) {
  Expects(client.instance != nullptr, "latency client exists");
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
}

void CheckLatency(std::string const& trace, std::string_view event, std::span<int64_t const> sent) {
  Expects(!event.empty(), "trace event is named");
  Expects(!sent.empty(), "client sent measured events");
  auto prefix = std::format("trace {} t=", event);
  std::vector<int64_t> latency;
  std::istringstream lines(trace);
  for (std::string line; std::getline(lines, line); ) {
    auto at = line.find(prefix);
    if (at == std::string::npos) continue;
    if (event == "key" && !line.contains(" code=30 ")) continue;
    ASSERT_LT(latency.size(), sent.size());
    auto elapsed = std::stoll(line.substr(at + prefix.size())) - sent[latency.size()];
    EXPECT_GE(elapsed, 0);
    latency.push_back(elapsed);
  }
  ASSERT_EQ(latency.size(), sent.size()) << event;
  std::ranges::sort(latency);
  auto p95 = latency[(latency.size() * 95 + 99) / 100 - 1];
  testing::Test::RecordProperty(std::string(event) + "_p95_ms", p95);
  SDL_Log("latency %.*s count=%zu p95=%lld ms", int(event.size()), event.data(), latency.size(), static_cast<long long>(p95));
  EXPECT_LT(p95, 40) << event;
}
}

TEST_F(Sample, InputAndClipboardUnderTightVideo) {
  Expects(process == nullptr, "sample has not started");
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.begin() + 1, {"SDL_AUDIO_DRIVER=rdp", "SDL_RDP_TRACE=1", "SDL_LOGGING=video=info"});
  arguments.insert(arguments.end(), {"--tone", "--tight"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  auto port = Number(std::string_view(line).substr(5));
  ASSERT_TRUE(Read("audio device=RDP client freq=44100"));
  std::jthread drain([&](std::stop_token stop) {
    Expects(process != nullptr, "trace source is running");
    std::string output;
    while (!stop.stop_requested()) process->Line(output, Clock::now() + 10ms);
  });
  Client client(port, true, 640, 480);
  Headless::ClipboardClient clipboard(client);
  Headless::SoundClient audio(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  Headless::FrameObserver frames(client);
  ASSERT_TRUE(client.Until([&] {
    if (!frames.ids.empty()) frames.Ack();
    return !audio.received.empty() && clipboard.accepted.load() > 0;
  }));
  std::vector<int64_t> keys, clips;
  auto start = Clock::now();
  auto before = frames.ids.size();
  for (unsigned i = 0; i < 60; ++i) {
    while (Clock::now() < start + i * 50ms) {
      ASSERT_TRUE(client.Pump(1));
      if (!frames.ids.empty()) ASSERT_TRUE(frames.Ack());
    }
    keys.push_back(SendTime(client));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input,
      i % 2 ? KBD_FLAGS_RELEASE : KBD_FLAGS_DOWN, 0x1e));
    clips.push_back(SendTime(client));
    ASSERT_EQ(clipboard.Offer({BYTE('A' + i), 0, 0, 0}), CHANNEL_RC_OK);
  }
  while (Clock::now() < start + 3s) {
    ASSERT_TRUE(client.Pump(1));
    if (!frames.ids.empty()) ASSERT_TRUE(frames.Ack());
  }
  EXPECT_GE(frames.ids.size() - before, 60u);
  ASSERT_NO_FATAL_FAILURE(Escape(client));
  drain.request_stop();
  drain.join();
  CheckLatency(process->transcript, "key", keys);
  CheckLatency(process->transcript, "clipboard", clips);
}
}
