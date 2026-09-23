#include "_detail/sample-fixture.hpp"
#include <future>
#include <sys/socket.h>

namespace SampleGate {
namespace {
int64_t WallTime() {
  Expects(SDL_WasInit(SDL_INIT_VIDEO) != 0, "sample video is initialized");
  return std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count();
}

void AcknowledgeFor(Client& client, Headless::FrameObserver& frames, std::chrono::milliseconds duration) {
  Expects(duration.count() > 0, "observation interval is positive");
  auto end = Clock::now() + duration;
  std::size_t acknowledged = 0;
  while (Clock::now() < end) {
    ASSERT_TRUE(client.Pump(1));
    if (frames.ids.size() == acknowledged) continue;
    ASSERT_TRUE(frames.Ack());
    acknowledged = frames.ids.size();
  }
}

void ResizeTo(Client& client, Headless::DisplayClient& display, unsigned width) {
  Expects(display.ready.load(), "display channel is ready");
  ASSERT_TRUE(display.Layout(width, 800));
  ASSERT_TRUE(client.Until([&] { return client.instance->context->gdi->width == int(width); }));
}

void InterruptAcknowledgements(Client& client, Headless::FrameObserver& frames,
                               Headless::DisplayClient& display, bool resize) {
  Expects(!frames.ids.empty(), "normal acknowledgements preceded interruption");
  if (resize) {
    display.finalization_delay = 600ms;
    ResizeTo(client, display, 1066);
    AcknowledgeFor(client, frames, 1s);
    ResizeTo(client, display, 1280);
  } else {
    auto end = Clock::now() + 600ms;
    while (Clock::now() < end) Expects(client.Pump(1), "client pumps while holding acknowledgement");
  }
}

void DelayAcknowledgements(Client& client, Headless::FrameObserver& frames) {
  Expects(!frames.ids.empty(), "client received frames");
  auto end = Clock::now() + 2s;
  auto next = frames.ids.size();
  while (Clock::now() < end) {
    ASSERT_TRUE(client.Pump(1));
    while (next < frames.ids.size() && Clock::now() - frames.received[next] >= 100ms) {
      ASSERT_TRUE(frames.update->SurfaceFrameAcknowledge(frames.update->context, frames.ids[next]));
      ++next;
    }
  }
  ASSERT_TRUE(frames.Ack());
}

int BoundReceiveBuffer(Client& client) {
  Expects(client.instance != nullptr, "client exists");
  std::array<HANDLE, 64> handles{};
  auto count = freerdp_get_event_handles(client.instance->context, handles.data(), handles.size());
  Expects(count > 0, "connected client has transport events");
  unsigned sockets = 0;
  int receiver = -1;
  for (auto event : std::span(handles).first(count)) {
    auto descriptor = GetEventFileDescriptor(event);
    int type = 0;
    socklen_t size = sizeof(type);
    if (getsockopt(descriptor, SOL_SOCKET, SO_TYPE, &type, &size) != 0 || type != SOCK_STREAM) continue;
    // Two raw frames must exceed socket buffering so a stopped reader keeps the write blocked.
    int bytes = 256 * 1024;
    Expects(setsockopt(descriptor, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes)) == 0,
            "client receive buffer is bounded");
    ++sockets;
    receiver = descriptor;
  }
  Expects(sockets == 1, "direct client has one TCP transport");
  return receiver;
}

struct Observation {
  int scenario;
  int64_t started, resumed = 0, reset = 0, finished = 0;
};

std::vector<Observation> Exercise(unsigned port, int scenario, std::string_view mode) {
  Expects(port > 0, "sample listener is open");
  Client client(port, true, 1280, 800);
  Headless::DisplayClient display(client);
  Expects(freerdp_connect(client.instance.get()), "headless client connects");
  auto receiver = mode == "auto-sender" && scenario >= 3 ? BoundReceiveBuffer(client) : -1;
  Headless::FrameObserver frames(client);
  Expects(client.Until([&] { return display.ready.load(); }), "display channel opens");
  AcknowledgeFor(client, frames, 2s);
  std::vector<Observation> observations;
  auto last = scenario == 0 ? 1 : scenario;
  for (; scenario <= last; ++scenario) {
    Observation observed{scenario, WallTime()};
    if (scenario < 2) InterruptAcknowledgements(client, frames, display, scenario == 1);
    else if (scenario == 2 || (scenario == 4 && mode != "auto-sender"))
      DelayAcknowledgements(client, frames);
    else {
      std::this_thread::sleep_for(1s);
      int bytes = 4 * 1024 * 1024;
      Expects(setsockopt(receiver, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes)) == 0,
              "resumed reader has room to drain queued frames");
    }
    if (scenario == 0 && mode == "auto-client-average") {
      AcknowledgeFor(client, frames, 1s);
      observed.reset = WallTime();
      ResizeTo(client, display, 1152);
    }
    if (scenario == 4) {
      observed.reset = WallTime();
      ResizeTo(client, display, 1066);
    }
    observed.resumed = WallTime();
    AcknowledgeFor(client, frames, 3s);
    observed.finished = WallTime();
    observations.push_back(observed);
  }
  return observations;
}

std::string TraceDuring(std::string const& trace, int64_t begin, int64_t end) {
  Expects(begin < end, "trace interval is positive");
  std::string result;
  std::istringstream lines(trace);
  for (std::string line; std::getline(lines, line); ) {
    auto at = line.find(" t=");
    if (at == std::string::npos) continue;
    auto time = std::stoll(line.substr(at + 3));
    if (time >= begin && time < end) result += line + '\n';
  }
  return result;
}

void ResetRecovery(std::string const& trace, Observation const& observed) {
  Expects(observed.reset > observed.started, "congestion precedes resize");
  auto before = TraceDuring(trace, observed.started, observed.reset);
  auto after = TraceDuring(trace, observed.reset, observed.finished);
  EXPECT_NE(before.find("trace refresh"), std::string::npos);
  EXPECT_NE(after.find("hz=60"), std::string::npos);
}

void AverageFloor(std::string const& trace, Observation const& observed) {
  Expects(observed.reset > observed.started, "held acknowledgement precedes resize");
  std::istringstream lines(TraceDuring(trace, observed.started, observed.reset));
  unsigned minimum = 60;
  for (std::string line; std::getline(lines, line); ) {
    auto at = line.find(" hz=");
    if (at == std::string::npos) continue;
    auto rate = unsigned(std::stoul(line.substr(at + 4)));
    EXPECT_GE(rate, 10u);
    minimum = std::min(minimum, rate);
  }
  testing::Test::RecordProperty("held_minimum_refresh", minimum);
  EXPECT_EQ(minimum, 10u);
}

std::vector<int64_t> PresentTimes(std::string const& trace) {
  Expects(!trace.empty(), "sample emitted trace output");
  std::vector<int64_t> presents;
  std::istringstream lines(trace);
  for (std::string line; std::getline(lines, line); ) {
    auto at = line.find("trace present t=");
    if (at != std::string::npos) presents.push_back(std::stoll(line.substr(at + 16)));
  }
  return presents;
}

void Recovery(std::string const& trace, int64_t resumed, std::string const& prefix) {
  Expects(resumed > 0, "acknowledgements resumed at a wall-clock timestamp");
  auto presents = PresentTimes(trace);
  ASSERT_FALSE(presents.empty());
  for (int second = 0; second < 3; ++second) {
    auto begin = resumed + second * 1000;
    auto count = std::ranges::count_if(presents, [&](auto t) { return t >= begin && t < begin + 1000; });
    testing::Test::RecordProperty(prefix + "presents_second_" + std::to_string(second), count);
    EXPECT_GE(count, second ? 30 : 20) << "second " << second << " after resuming acknowledgements";
  }
  auto minimum = presents.size();
  for (auto begin = resumed; begin <= resumed + 2000; ++begin)
    minimum = std::min(minimum, std::size_t(std::ranges::count_if(presents, [&](auto t) { return t >= begin && t < begin + 1000; })));
  testing::Test::RecordProperty(prefix + "minimum_rolling_second", minimum);
  EXPECT_GE(minimum, 20u);
}
}

class VsyncRecovery : public Sample, public testing::WithParamInterface<const char*> {
protected:
  std::vector<Observation> observations;
  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;
  SDL_LogOutputFunction output = nullptr;
  void* userdata = nullptr;
  std::string previous_trace;
  SDL_LogPriority priority = SDL_LOG_PRIORITY_INVALID;
  void SetUp() override {
    Expects(window == nullptr, "fixture has no window");
    Sample::SetUp();
    if (auto value = std::getenv("SDL_RDP_TRACE")) previous_trace = value;
    ASSERT_EQ(setenv("SDL_RDP_TRACE", "1", 1), 0);
    priority = SDL_GetLogPriority(SDL_LOG_CATEGORY_VIDEO);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_VIDEO, SDL_LOG_PRIORITY_INFO);
    SDL_GetLogOutputFunction(&output, &userdata);
    SDL_SetLogOutputFunction([](void* user, int, SDL_LogPriority, char const* text) {
      Headless::Logs::Collect(user, SDLRDP_LOG_INFO, text);
    }, &logs);
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_REFRESH", GetParam()));
    ASSERT_NO_FATAL_FAILURE(CreateRenderer());
  }
  void CreateRenderer() {
    Expects(window == nullptr, "window has not been created");
    for (auto [key, value] : {std::pair{SDL_HINT_VIDEO_DRIVER, "rdp"}, {"SDL_RDP_PORT", "0"},
         {"SDL_RDP_BIND", "127.0.0.1"}, {"SDL_RDP_CODEC", "raw"}, {"SDL_RDP_WIDTH", "1280"},
         {"SDL_RDP_HEIGHT", "800"}, {"SDL_RDP_VSYNC", "0"}}) ASSERT_TRUE(SDL_SetHint(key, value));
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_CERT_DIR", certificates.Path().c_str()));
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_BACKEND", (BuildRoot() / "sources/sdl-rdp-backend.so/libsdl-rdp-backend.so").c_str()));
    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
    window = SDL_CreateWindow("vsync recovery", 1280, 800, SDL_WINDOW_FULLSCREEN);
    ASSERT_NE(window, nullptr);
    renderer = SDL_CreateRenderer(window, "software");
    ASSERT_NE(renderer, nullptr);
    ASSERT_TRUE(SDL_SetRenderVSync(renderer, 1));
  }
  void Run(int scenario) {
    Expects(renderer != nullptr, "vsync renderer exists");
    auto port = SDL_GetNumberProperty(SDL_GetDisplayProperties(SDL_GetPrimaryDisplay()), SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0);
    auto client = std::async(std::launch::async, Exercise, unsigned(port), scenario, std::string_view(GetParam()));
    unsigned frame = 0;
    while (client.wait_for(0ms) != std::future_status::ready) {
      SDL_PumpEvents();
      ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, ++frame % 256, 0, 0, 255));
      ASSERT_TRUE(SDL_RenderClear(renderer));
      ASSERT_TRUE(SDL_RenderPresent(renderer));
    }
    observations = client.get();
    if (auto directory = std::getenv("SDLRDP_TEST_EVIDENCE")) {
      auto path = fs::path(directory) / (std::string(GetParam()) + "-" + std::to_string(scenario) + ".log");
      std::ofstream file(path);
      for (auto const& observed : observations)
        file << "scenario=" << observed.scenario << " started=" << observed.started
             << " resumed=" << observed.resumed << " reset=" << observed.reset << '\n';
      file << logs.Text(true);
    }

  }
  void TearDown() override {
    Expects(output != nullptr, "previous log callback was saved");
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    SDL_SetLogOutputFunction(output, userdata);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_VIDEO, priority);
    for (auto hint : {SDL_HINT_VIDEO_DRIVER, "SDL_RDP_PORT", "SDL_RDP_BIND", "SDL_RDP_CODEC", "SDL_RDP_WIDTH",
         "SDL_RDP_HEIGHT", "SDL_RDP_VSYNC", "SDL_RDP_REFRESH", "SDL_RDP_CERT_DIR", "SDL_RDP_BACKEND"}) SDL_ResetHint(hint);
    if (previous_trace.empty()) unsetenv("SDL_RDP_TRACE");
    else setenv("SDL_RDP_TRACE", previous_trace.c_str(), 1);
    Sample::TearDown();
  }
};

TEST_P(VsyncRecovery, HeldAcknowledgementAndDisplayChannelResize) {
  Expects(renderer != nullptr, "fixture created a renderer");
  Run(0);
  auto trace = logs.Text(true);
  ASSERT_EQ(observations.size(), 2u);
  for (auto const& observed : observations) {
    SCOPED_TRACE(observed.scenario);
    if (observed.reset) {
      AverageFloor(trace, observed);
      ResetRecovery(trace, observed);
    }
    Recovery(trace, observed.resumed, observed.scenario == 0 ? "held_" : "resize_");
  }
}
class ClientRecovery : public VsyncRecovery {};
TEST_P(ClientRecovery, DelayedAcknowledgementsDropAndRecover) {
  Expects(renderer != nullptr, "fixture created a renderer");
  Run(2);
  auto times = PresentTimes(logs.Text(true));
  auto resumed = observations.front().resumed;
  auto count = [&](int64_t begin) { return std::ranges::count_if(times,
    [&](auto time) { return time >= begin && time < begin + 1000; }); };
  RecordProperty("slow_second", count(resumed - 1000));
  EXPECT_LT(count(resumed - 1000), 20);
  EXPECT_GE(count(resumed), 30);
  Recovery(logs.Text(true), resumed, "prompt_");
}
class SenderRecovery : public VsyncRecovery {};
TEST_P(SenderRecovery, SocketPauseDropAndRecover) {
  Expects(renderer != nullptr, "fixture created a renderer");
  Run(3);
  auto trace = logs.Text(true);
  auto const& observed = observations.front();
  auto times = PresentTimes(trace);
  auto count = [&](int64_t begin) { return std::ranges::count_if(times,
    [&](auto time) { return time >= begin && time < begin + 1000; }); };
  RecordProperty("stalled_second", count(observed.resumed - 1000));
  EXPECT_LT(count(observed.resumed - 1000), 20);
  EXPECT_GE(count(observed.resumed), 30);
  EXPECT_NE(TraceDuring(trace, observed.started, observed.resumed).find("hz=10"), std::string::npos);
  Recovery(trace, observed.resumed, "drained_");
}
class AdaptiveRecovery : public VsyncRecovery {};
TEST_P(AdaptiveRecovery, ResizeRestartsAtCeiling) {
  Expects(renderer != nullptr, "fixture created a renderer");
  Run(4);
  auto trace = logs.Text(true);
  auto const& observed = observations.front();
  ResetRecovery(trace, observed);
  Recovery(trace, observed.resumed, "reset_");
}
INSTANTIATE_TEST_SUITE_P(Rates, VsyncRecovery,
  testing::Values("60", "auto-client", "auto-client-average", "auto-sender"));
INSTANTIATE_TEST_SUITE_P(Client, ClientRecovery, testing::Values("auto-client"));
INSTANTIATE_TEST_SUITE_P(Sender, SenderRecovery, testing::Values("auto-sender"));
INSTANTIATE_TEST_SUITE_P(Adaptive, AdaptiveRecovery,
  testing::Values("auto-client", "auto-client-average", "auto-sender"));
}
