#include "_detail/sample-fixture.hpp"
#include <future>

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

int64_t Exercise(unsigned port, bool resize) {
  Expects(port > 0, "sample listener is open");
  Client client(port, true, 1280, 800);
  Headless::DisplayClient display(client);
  Expects(freerdp_connect(client.instance.get()), "headless client connects");
  Headless::FrameObserver frames(client);
  Expects(client.Until([&] { return display.ready.load(); }), "display channel opens");
  AcknowledgeFor(client, frames, 2s);
  InterruptAcknowledgements(client, frames, display, resize);
  auto resumed = WallTime();
  AcknowledgeFor(client, frames, 3s);
  return resumed;
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

void Recovery(std::string const& trace, int64_t resumed) {
  Expects(resumed > 0, "acknowledgements resumed at a wall-clock timestamp");
  auto presents = PresentTimes(trace);
  ASSERT_FALSE(presents.empty());
  for (int second = 0; second < 3; ++second) {
    auto begin = resumed + second * 1000;
    auto count = std::ranges::count_if(presents, [&](auto t) { return t >= begin && t < begin + 1000; });
    testing::Test::RecordProperty("presents_second_" + std::to_string(second), count);
    EXPECT_GE(count, second ? 30 : 20) << "second " << second << " after resuming acknowledgements";
  }
  auto minimum = presents.size();
  for (auto begin = resumed; begin <= resumed + 2000; ++begin)
    minimum = std::min(minimum, std::size_t(std::ranges::count_if(presents, [&](auto t) { return t >= begin && t < begin + 1000; })));
  testing::Test::RecordProperty("minimum_rolling_second", minimum);
  EXPECT_GE(minimum, 20u);
}
}

class VsyncRecovery : public Sample {
protected:
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
  void Run(bool resize) {
    Expects(renderer != nullptr, "vsync renderer exists");
    auto port = SDL_GetNumberProperty(SDL_GetDisplayProperties(SDL_GetPrimaryDisplay()), SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0);
    auto client = std::async(std::launch::async, Exercise, unsigned(port), resize);
    unsigned frame = 0;
    while (client.wait_for(0ms) != std::future_status::ready) {
      SDL_PumpEvents();
      ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, ++frame % 256, 0, 0, 255));
      ASSERT_TRUE(SDL_RenderClear(renderer));
      ASSERT_TRUE(SDL_RenderPresent(renderer));
    }
    Recovery(logs.Text(true), client.get());
  }
  void TearDown() override {
    Expects(output != nullptr, "previous log callback was saved");
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    SDL_SetLogOutputFunction(output, userdata);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_VIDEO, priority);
    for (auto hint : {SDL_HINT_VIDEO_DRIVER, "SDL_RDP_PORT", "SDL_RDP_BIND", "SDL_RDP_CODEC", "SDL_RDP_WIDTH",
         "SDL_RDP_HEIGHT", "SDL_RDP_VSYNC", "SDL_RDP_CERT_DIR", "SDL_RDP_BACKEND"}) SDL_ResetHint(hint);
    if (previous_trace.empty()) unsetenv("SDL_RDP_TRACE");
    else setenv("SDL_RDP_TRACE", previous_trace.c_str(), 1);
    Sample::TearDown();
  }
};

TEST_F(VsyncRecovery, HeldAcknowledgement) {
  Expects(renderer != nullptr, "fixture created a renderer");
  Run(false);
}
TEST_F(VsyncRecovery, DisplayChannelResize) {
  Expects(renderer != nullptr, "fixture created a renderer");
  Run(true);
}
}
