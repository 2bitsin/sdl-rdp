#include <sdl-rdp/sample-gate.test/process/captured-logs.hpp>
#include <sdl-rdp/sample-gate.test/process/initialized-sdl.hpp>
#include <sdl-rdp/sample-gate.test/sample/launch.hpp>
#include <sdl-rdp/sample-gate.test/sample/sample.hpp>

#include <sdl-rdp/sample-gate.test/video/rate-exercise.hpp>

#include <SDL3/SDL.h>
#include <cstdint>
#include <future>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace sdl_rdp::integration::sample_test::detail::vsync {
using namespace std::chrono_literals;
using sdl_rdp::configuration::RefreshMode;
using sdl_rdp::sample_gate_test::process::Capture;
using sdl_rdp::sample_gate_test::process::CapturedLogs;
using sdl_rdp::sample_gate_test::process::Renderer;
using sdl_rdp::sample_gate_test::process::Window;
using sdl_rdp::sample_gate_test::sample::PrimaryDisplayPort;
using sdl_rdp::sample_gate_test::sample::Sample;
using sdl_rdp::sample_gate_test::sample::SetLoopbackHints;
using sdl_rdp::sample_gate_test::video::ExerciseRate;
using sdl_rdp::sample_gate_test::video::RateRecovery;
using sdl_rdp::utilities::Expects;

namespace {
struct RefreshCase {
  std::string_view hint;
  RefreshMode      mode;
};
auto operator<<(std::ostream& output, RefreshCase const& value) -> std::ostream& {
  return output << '"' << value.hint << '"';
}
}
class VsyncRecovery : public Sample, public testing::WithParamInterface<RefreshCase> {
protected:
  auto WhenFrameRendered(std::uint32_t& frame) -> void {
    SDL_PumpEvents();
    ASSERT_TRUE(SDL_SetRenderDrawColor(renderer.get(), ++frame % 256, 0, 0, 255));
    ASSERT_TRUE(SDL_RenderClear(renderer.get()));
    ASSERT_TRUE(SDL_RenderPresent(renderer.get()));
  }
  auto GivenRendererHints() -> void {
    ASSERT_TRUE(SetLoopbackHints(certificates.Path(), { { "SDL_RDP_CODEC" , "raw" },
                                                        { "SDL_RDP_WIDTH" , "640" },
                                                        { "SDL_RDP_HEIGHT", "480" },
                                                        { "SDL_RDP_VSYNC" , "0"   } }));
  }
  auto SetUp() -> void override {
    Expects(window == nullptr, "fixture has no window");
    ASSERT_NO_FATAL_FAILURE(Sample::SetUp());
    if (auto* value = std::getenv("SDL_RDP_TRACE")) previous_trace = value;
    ASSERT_EQ(setenv("SDL_RDP_TRACE", "1", 1), 0);
    captured.emplace(logs, Capture{ .video = SDL_LOG_PRIORITY_INFO });
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_REFRESH", std::string(GetParam().hint).c_str()));
    CreateRenderer();
  }
  auto CreateRenderer() -> void {
    Expects(window == nullptr, "window has not been created");
    ASSERT_NO_FATAL_FAILURE(GivenRendererHints());
    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
    window.reset(SDL_CreateWindow("vsync recovery", 640, 480, SDL_WINDOW_FULLSCREEN));
    ASSERT_NE(window, nullptr);
    renderer.reset(SDL_CreateRenderer(window.get(), "software"));
    ASSERT_NE(renderer, nullptr);
    ASSERT_TRUE(SDL_SetRenderVSync(renderer.get(), 1));
  }
  auto RenderWhile(std::future<void>& client) -> void {
    Expects(renderer != nullptr, "vsync renderer exists");
    std::uint32_t frame = 0;
    while (client.wait_for(0ms) != std::future_status::ready) {
      ASSERT_NO_FATAL_FAILURE(WhenFrameRendered(frame));
    }
  }
  auto Run(RateRecovery recovery) -> void {
    Expects(renderer != nullptr, "vsync renderer exists");
    auto client = std::async(std::launch::async, ExerciseRate, PrimaryDisplayPort(), std::ref(logs), GetParam().mode,
                             recovery);
    ASSERT_NO_FATAL_FAILURE(RenderWhile(client));
    client.get();
  }
  auto TearDown() -> void override {
    renderer.reset();
    window.reset();
    SDL_Quit();
    captured.reset();
    for (auto const* hint : { SDL_HINT_VIDEO_DRIVER, "SDL_RDP_PORT", "SDL_RDP_BIND", "SDL_RDP_CODEC", "SDL_RDP_WIDTH",
                              "SDL_RDP_HEIGHT", "SDL_RDP_VSYNC", "SDL_RDP_REFRESH", "SDL_RDP_CERT_DIR" })
      SDL_ResetHint(hint);
    if (previous_trace.empty())
      unsetenv("SDL_RDP_TRACE");
    else
      setenv("SDL_RDP_TRACE", previous_trace.c_str(), 1);
    Sample::TearDown();
  }
  Window                      window;
  Renderer                    renderer;
  std::optional<CapturedLogs> captured;
  std::string                 previous_trace;
};

TEST_P(VsyncRecovery, HeldAcknowledgementAndDisplayChannelResize) {
  ASSERT_NO_FATAL_FAILURE(Run(RateRecovery::InPlace));
  Run(RateRecovery::AfterResize);
}
class ClientRecovery : public VsyncRecovery { };
TEST_P(ClientRecovery, DelayedAcknowledgementsDropAndRecover) {
  Run(RateRecovery::InPlace);
}
class SenderRecovery : public VsyncRecovery { };
TEST_P(SenderRecovery, SocketPauseDropAndRecover) {
  Run(RateRecovery::InPlace);
}
class AdaptiveRecovery : public VsyncRecovery { };
TEST_P(AdaptiveRecovery, ResizeRestartsAtCeiling) {
  Run(RateRecovery::AfterResize);
}
constexpr RefreshCase FixedRefresh  { .hint = "60", .mode = RefreshMode::Fixed                    };
constexpr RefreshCase ClientRefresh { .hint = "auto-client", .mode = RefreshMode::Client          };
constexpr RefreshCase AverageRefresh{ .hint = "auto-client-average", .mode = RefreshMode::Average };
constexpr RefreshCase SenderRefresh { .hint = "auto-sender", .mode = RefreshMode::Sender          };
INSTANTIATE_TEST_SUITE_P(Rates, VsyncRecovery,
                         testing::Values(FixedRefresh, ClientRefresh, AverageRefresh, SenderRefresh));
INSTANTIATE_TEST_SUITE_P(Client, ClientRecovery, testing::Values(ClientRefresh));
INSTANTIATE_TEST_SUITE_P(Sender, SenderRecovery, testing::Values(SenderRefresh));
INSTANTIATE_TEST_SUITE_P(Adaptive, AdaptiveRecovery, testing::Values(ClientRefresh, AverageRefresh, SenderRefresh));
}
