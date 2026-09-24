#include "_detail/sample-fixture.hpp"

#include "_detail/rate-exercise.hpp"

#include <future>
#include <ostream>

namespace SampleGate {
namespace {
struct RefreshCase {
  char const*          hint;
  Backend::RefreshMode mode;
};
void PrintTo(RefreshCase const& value, std::ostream* output) {
  *output << '"' << value.hint << '"';
}
}
class VsyncRecovery : public Sample, public testing::WithParamInterface<RefreshCase> {
protected:
  void WhenFrameRendered(unsigned& frame) {
    SDL_PumpEvents();
    ASSERT_TRUE(SDL_SetRenderDrawColor(renderer, ++frame % 256, 0, 0, 255));
    ASSERT_TRUE(SDL_RenderClear(renderer));
    ASSERT_TRUE(SDL_RenderPresent(renderer));
  }
  void GivenRendererHints() {
    for (auto [key, value] : { std::pair{ SDL_HINT_VIDEO_DRIVER, "rdp" },
                               { "SDL_RDP_PORT"  , "0"         },
                               { "SDL_RDP_BIND"  , "127.0.0.1" },
                               { "SDL_RDP_CODEC" , "raw"       },
                               { "SDL_RDP_WIDTH" , "640"       },
                               { "SDL_RDP_HEIGHT", "480"       },
                               { "SDL_RDP_VSYNC" , "0"         } })
      ASSERT_TRUE(SDL_SetHint(key, value));
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_CERT_DIR", certificates.Path().c_str()));
    ASSERT_TRUE(
        SDL_SetHint("SDL_RDP_BACKEND", (BuildRoot() / "sources/sdl-rdp-backend.so/libsdl-rdp-backend.so").c_str()));
  }
  void SetUp() override {
    Expects(window == nullptr, "fixture has no window");
    Sample::SetUp();
    if (auto* value = std::getenv("SDL_RDP_TRACE")) previous_trace = value;
    ASSERT_EQ(setenv("SDL_RDP_TRACE", "1", 1), 0);
    priority = SDL_GetLogPriority(SDL_LOG_CATEGORY_VIDEO);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_VIDEO, SDL_LOG_PRIORITY_INFO);
    SDL_GetLogOutputFunction(&output, &userdata);
    SDL_SetLogOutputFunction([](void* user, int, SDL_LogPriority,
                                char const* text) { Headless::Logs::Collect(user, SDLRDP_LOG_INFO, text); },
                             &logs);
    ASSERT_TRUE(SDL_SetHint("SDL_RDP_REFRESH", GetParam().hint));
    CreateRenderer();
  }
  void CreateRenderer() {
    Expects(window == nullptr, "window has not been created");
    GivenRendererHints();
    if (::testing::Test::HasFatalFailure()) return;
    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO));
    window = SDL_CreateWindow("vsync recovery", 640, 480, SDL_WINDOW_FULLSCREEN);
    ASSERT_NE(window, nullptr);
    renderer = SDL_CreateRenderer(window, "software");
    ASSERT_NE(renderer, nullptr);
    ASSERT_TRUE(SDL_SetRenderVSync(renderer, 1));
  }
  void RenderWhile(std::future<void>& client) {
    Expects(renderer != nullptr, "vsync renderer exists");
    unsigned frame = 0;
    while (client.wait_for(0ms) != std::future_status::ready) {
      WhenFrameRendered(frame);
      if (::testing::Test::HasFatalFailure()) return;
    }
  }
  void Run(RateRecovery recovery) {
    Expects(renderer != nullptr, "vsync renderer exists");
    auto port   = SDL_GetNumberProperty(SDL_GetDisplayProperties(SDL_GetPrimaryDisplay()),
                                     SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0);
    auto client = std::async(std::launch::async, ExerciseRate, unsigned(port), std::ref(logs), GetParam().mode,
                             recovery);
    ASSERT_NO_FATAL_FAILURE(RenderWhile(client));
    client.get();
  }
  void TearDown() override {
    Expects(output != nullptr, "previous log callback was saved");
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    SDL_SetLogOutputFunction(output, userdata);
    SDL_SetLogPriority(SDL_LOG_CATEGORY_VIDEO, priority);
    for (auto const* hint :
         { SDL_HINT_VIDEO_DRIVER, "SDL_RDP_PORT", "SDL_RDP_BIND", "SDL_RDP_CODEC", "SDL_RDP_WIDTH", "SDL_RDP_HEIGHT",
           "SDL_RDP_VSYNC", "SDL_RDP_REFRESH", "SDL_RDP_CERT_DIR", "SDL_RDP_BACKEND" })
      SDL_ResetHint(hint);
    if (previous_trace.empty())
      unsetenv("SDL_RDP_TRACE");
    else
      setenv("SDL_RDP_TRACE", previous_trace.c_str(), 1);
    Sample::TearDown();
  }
  SDL_Window*           window         = nullptr;
  SDL_Renderer*         renderer       = nullptr;
  SDL_LogOutputFunction output         = nullptr;
  void*                 userdata       = nullptr;
  std::string           previous_trace;
  SDL_LogPriority       priority       = SDL_LOG_PRIORITY_INVALID;
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
constexpr RefreshCase FixedRefresh  { .hint = "60", .mode = Backend::RefreshMode::Fixed                    };
constexpr RefreshCase ClientRefresh { .hint = "auto-client", .mode = Backend::RefreshMode::Client          };
constexpr RefreshCase AverageRefresh{ .hint = "auto-client-average", .mode = Backend::RefreshMode::Average };
constexpr RefreshCase SenderRefresh { .hint = "auto-sender", .mode = Backend::RefreshMode::Sender          };
INSTANTIATE_TEST_SUITE_P(Rates, VsyncRecovery,
                         testing::Values(FixedRefresh, ClientRefresh, AverageRefresh, SenderRefresh));
INSTANTIATE_TEST_SUITE_P(Client, ClientRecovery, testing::Values(ClientRefresh));
INSTANTIATE_TEST_SUITE_P(Sender, SenderRecovery, testing::Values(SenderRefresh));
INSTANTIATE_TEST_SUITE_P(Adaptive, AdaptiveRecovery, testing::Values(ClientRefresh, AverageRefresh, SenderRefresh));
}
