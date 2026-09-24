#include <sdl-rdp/sample-gate.test/client-steps.hpp>
#include <sdl-rdp/sample-gate.test/first-frame-size.hpp>
#include <sdl-rdp/sample-gate.test/full-desktop-frames.hpp>
#include <sdl-rdp/sample-gate.test/process.hpp>
#include <sdl-rdp/sample-gate.test/sample-launch.hpp>
#include <sdl-rdp/sample-gate.test/sample.hpp>

#include <SDL3/SDL.h>
#include <sdl-rdp/headless-client.test/display-client.hpp>
#include <sdl-rdp/headless-client.test/frame-observer.hpp>
#include <sdl-rdp/headless-client.test/input-steps.hpp>
#include <algorithm>
#include <cstddef>
#include <fstream>
#include <memory>
#include <ranges>
#include <string>
#include <thread>
#include <utility>

namespace SampleGate {
namespace {
class FullscreenSample : public SampleGate::Sample {
protected:
  static auto ThenWindowedPicture(Client& client) -> void {
    ASSERT_TRUE(client.UntilDesktop(640, 480));
  }
  auto ThenRelativeMotion(Client& client, rdpInput* input) -> void {
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 330, 192));
    ASSERT_TRUE(ReadInput(client, "event MOUSE_MOTION "));
    EXPECT_TRUE(line.contains(" xrel=10 yrel=-35 ")) << line;
  }
  auto ThenRelativeAspectMouse(Client& client) -> void {
    auto* input = client.Instance()->context->input;
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 320, 240));
    ASSERT_TRUE(Read("event MOUSE_MOTION "));
    ASSERT_NO_FATAL_FAILURE(WhenRelative(client));
    ThenRelativeMotion(client, input);
  }
  auto ThenAbsoluteAspectMouse(Client& client) -> void {
    ASSERT_TRUE(Read("event FOCUS_GAINED "));
    ASSERT_NO_FATAL_FAILURE(ThenWindowedPicture(client));
    ASSERT_TRUE(freerdp_input_send_mouse_event(client.Instance()->context->input, PTR_FLAGS_MOVE, 639, 479));
    ASSERT_TRUE(Read("event MOUSE_MOTION "));
    EXPECT_TRUE(line.contains(" x=639 y=349 ")) << line;
  }
  auto ThenWindowedGeometry(Client& client) -> void {
    ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=640x480"));
    ThenWindowedPicture(client);
  }
  static auto ThenExclusiveSize(FirstFrameSize const& frame) -> void {
    EXPECT_EQ(frame.Width(), 320);
    EXPECT_EQ(frame.Height(), 200);
  }
};
TEST_F(FullscreenSample, ExplicitFullscreenBeforeConnect) {
  ASSERT_NO_FATAL_FAILURE(GivenFullscreen());
  auto port = AnnouncedPort(line);
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  Client         client(port, true, 1280, 800);
  FirstFrameSize frame(client);
  ASSERT_TRUE(client.Connect());
  ASSERT_TRUE(client.Until([&] { return frame.Received(); }));
  ASSERT_NO_FATAL_FAILURE(ThenExclusiveSize(frame));
  Escape(client);
}

TEST_F(FullscreenSample, ExplicitFullscreenRestoresWindow) {
  ASSERT_NO_FATAL_FAILURE(GivenFullscreen());
  auto client = AnnouncedClient(1280, 800);
  ASSERT_NO_FATAL_FAILURE(ConnectExposed(client));
  ASSERT_NO_FATAL_FAILURE(ThenExplicitGeometry(client));
  SDL_Log("trace explicit screen=1280x800 gdi=320x200");
  ASSERT_NO_FATAL_FAILURE(PressFullscreenKey(client));
  ASSERT_NO_FATAL_FAILURE(ThenWindowedGeometry(client));
  SDL_Log("trace leave gdi=640x480 screen=1280x800");
  ASSERT_NO_FATAL_FAILURE(PressFullscreenKey(client));
  ASSERT_NO_FATAL_FAILURE(ThenExplicitGeometry(client));
  SDL_Log("trace reenter gdi=320x200");
  Escape(client);
}

TEST_F(FullscreenSample, ExplicitFullscreenSurvivesScreenChange) {
  ASSERT_NO_FATAL_FAILURE(GivenFullscreen());
  auto                          client  = AnnouncedClient(1280, 800);
  Headless::DisplayClient const display(client);
  ASSERT_NO_FATAL_FAILURE(ConnectExposed(client));
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ASSERT_NO_FATAL_FAILURE(ChangeMonitor(client));
  ASSERT_TRUE(ReadInput(client, "event DISPLAY_DESKTOP_MODE_CHANGED type="
                                    + std::to_string(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED)
                                    + " width=1920 height=1080"));
  ASSERT_NO_FATAL_FAILURE(ThenExplicitGeometry(client));
  SDL_Log("trace screen=1920x1080 gdi=320x200");
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x3e));
  ASSERT_NO_FATAL_FAILURE(ThenWindowedGeometry(client));
  SDL_Log("trace leave gdi=640x480 screen=1920x1080");
  Escape(client);
}

class RefreshMode : public FullscreenSample, public testing::WithParamInterface<char const*> {
protected:
  auto ThenDeclaredRate() -> void {
    std::string_view const kind = GetParam();
    auto const*            rate = kind == "exclusive" ? "90" : kind == "borderless" ? "75" : "60";
    EXPECT_TRUE(
        process->Transcript().contains(std::string("refresh=") + rate + " numerator=" + rate + " denominator=1"));
  }
  auto WhenFocusSynchronized(Client& client) -> void {
    ASSERT_TRUE(ReadInput(client, "event FOCUS_GAINED "));
    ASSERT_NO_FATAL_FAILURE(client.Tap(0x30));
    ASSERT_TRUE(ReadInput(client, "event KEY_UP "));
  }
  auto Count(std::string_view event) -> std::size_t {
    Expects(!event.empty(), "event name is supplied");
    std::size_t total = 0;
    for (std::size_t at = 0; (at = process->Transcript().find(event, at)) != std::string::npos; at += event.size())
      ++total;
    return total;
  }
  auto ModeChanges() -> std::pair<std::size_t, std::size_t> {
    return { Count("event DISPLAY_DESKTOP_MODE_CHANGED "), Count("event DISPLAY_CURRENT_MODE_CHANGED ") };
  }
  auto Start() -> void {
    Expects(process == nullptr, "sample has not started");
    std::string_view const kind        = GetParam();
    Words                  environment { "SDL_RDP_WIDTH=640", "SDL_RDP_HEIGHT=480" };
    Words                  options     { "--tight"                                 };
    if (kind == "borderless") environment.emplace_back("SDL_RDP_REFRESH=75");
    if (kind == "exclusive") environment.emplace_back("SDL_RDP_INI=" + RefreshIni().string());
    if (kind != "windowed") options.emplace_back("--fullscreen");
    if (kind == "exclusive") options.append_range(Words{ "--mode", "320x200" });
    GivenProcess(environment, options);
  }
  auto RefreshIni() -> fs::path {
    auto ini = certificates.Path() / "refresh.ini";
    std::ofstream(ini) << "SDL_RDP_REFRESH=90\n";
    return ini;
  }
  static auto AcknowledgeCadence(Client& client, Headless::FrameObserver& frames) -> void {
    for (std::size_t i = 0; i < 6; ++i) {
      auto before = frames.Frames().size();
      std::this_thread::sleep_for(i % 2 ? 20ms : 120ms);
      ASSERT_TRUE(frames.Ack());
      ASSERT_TRUE(client.Until([&] { return frames.Frames().size() >= before + 2; }));
    }
  }
  auto Observe(Client& client, Headless::FrameObserver& frames) -> void {
    Expects(process != nullptr, "sample is running");
    ASSERT_NO_FATAL_FAILURE(WhenFocusSynchronized(client));
    auto const modes = ModeChanges();
    ThenDeclaredRate();
    ASSERT_NO_FATAL_FAILURE(AcknowledgeCadence(client, frames));
    ASSERT_NO_FATAL_FAILURE(WhenKeyDown(client));
    EXPECT_EQ(ModeChanges(), modes);
  }
};

TEST_P(RefreshMode, AcknowledgementsPreserveDeclaredRate) {
  ASSERT_NO_FATAL_FAILURE(Start());
  auto client = AnnouncedClient(1024, 768);
  ASSERT_NO_FATAL_FAILURE(ConnectAcknowledging(client));
  Headless::FrameObserver frames(client);
  ASSERT_TRUE(client.Until([&] { return !frames.Frames().empty(); }));
  ASSERT_TRUE(frames.Ack());
  ASSERT_NO_FATAL_FAILURE(Observe(client, frames));
  Escape(client);
}

INSTANTIATE_TEST_SUITE_P(Window, RefreshMode, testing::Values("windowed", "borderless", "exclusive"));

class ExclusiveFullscreen : public FullscreenSample, public testing::WithParamInterface<char const*> {
protected:
  auto WhenDesktopModeChanges(Client& client, Headless::DisplayClient& display, FullDesktopFrames const& desktop)
      -> void {
    ASSERT_GT(desktop.Full(), 0u);
    ASSERT_TRUE(display.Layout(1920, 1080));
    ASSERT_TRUE(ReadInput(client, "event DISPLAY_DESKTOP_MODE_CHANGED type="
                                      + std::to_string(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED)
                                      + " width=1920 height=1080"));
  }
  static auto ThenNextWindow(Client& client, Headless::FrameObserver& frames) -> void {
    auto initial = frames.Frames().size();
    ASSERT_TRUE(frames.Ack());
    ASSERT_TRUE(client.Until([&] { return frames.Frames().size() >= initial + 2; }));
  }
  static auto ThenIncrementalPicture(Client& client, FullDesktopFrames const& desktop, std::size_t baseline,
                                     std::size_t deliveries) -> void {
    EXPECT_EQ(desktop.Full(), baseline);
    EXPECT_GT(desktop.Deliveries(), deliveries);
    EXPECT_EQ(client.Instance()->context->gdi->width, 320);
    EXPECT_EQ(client.Instance()->context->gdi->height, 200);
  }
  auto DelayAcknowledgement(Client& client, Headless::FrameObserver& frames) -> void {
    Expects(frames.Acknowledgements().size() >= 2, "two previous acknowledgements define the delay");
    ASSERT_NO_FATAL_FAILURE(WhenKeyDown(client));
    auto interval = frames.Acknowledgements().back() - frames.Acknowledgements()[frames.Acknowledgements().size() - 2];
    std::this_thread::sleep_until(frames.Acknowledgements().back() + (interval * 3));
  }
  auto IncrementalFrames(Client& client, Headless::FrameObserver& frames, FullDesktopFrames& desktop,
                         std::string_view change) -> void {
    Expects(!change.empty(), "frame trigger is named");
    auto baseline   = desktop.Full();
    auto before     = frames.Frames().size();
    auto deliveries = desktop.Deliveries();
    if (change == "delayed ack") {
      ASSERT_NO_FATAL_FAILURE(DelayAcknowledgement(client, frames));
    }
    ASSERT_TRUE(frames.Ack());
    ASSERT_TRUE(client.Until([&] { return frames.Frames().size() >= before + 2; }));
    ASSERT_NO_FATAL_FAILURE(ThenIncrementalPicture(client, desktop, baseline, deliveries));
    SDL_Log("trace exclusive %.*s gdi=%dx%d new_full_desktop=%zu frames=%zu", int(change.size()), change.data(),
            client.Instance()->context->gdi->width, client.Instance()->context->gdi->height, desktop.Full() - baseline,
            frames.Frames().size() - before);
  }
  auto Start() -> void {
    Expects(process == nullptr, "sample has not started");
    GivenProcess({ "SDL_RDP_CODEC=" + std::string(GetParam()) }, { "--fullscreen", "--mode", "320x200", "--partial" });
  }
  auto InitialFrames(Client& client, Headless::DisplayClient& display, Headless::FrameObserver& frames) -> void {
    Expects(process != nullptr, "sample is running");
    ASSERT_TRUE(client.Until([&] { return !frames.Frames().empty(); }));
    auto initial = frames.Frames().size();
    ASSERT_TRUE(frames.Ack());
    ASSERT_TRUE(ReadInput(client, "event FOCUS_GAINED "));
    ASSERT_TRUE(client.Until([&] { return display.Ready() && frames.Frames().size() >= initial + 2; }));
    ThenNextWindow(client, frames);
  }
};

TEST_P(ExclusiveFullscreen, DoesNotRepaintOnModeChanges) {
  ASSERT_NO_FATAL_FAILURE(Start());
  auto                    client  = AnnouncedClient(1280, 800);
  Headless::DisplayClient display(client);
  ASSERT_NO_FATAL_FAILURE(ConnectAcknowledging(client));
  FullDesktopFrames       desktop(client);
  Headless::FrameObserver frames(client);
  ASSERT_NO_FATAL_FAILURE(InitialFrames(client, display, frames));
  ASSERT_NO_FATAL_FAILURE(WhenDesktopModeChanges(client, display, desktop));
  ASSERT_NO_FATAL_FAILURE(IncrementalFrames(client, frames, desktop, "screen"));
  ASSERT_NO_FATAL_FAILURE(IncrementalFrames(client, frames, desktop, "delayed ack"));
  Escape(client);
}

INSTANTIATE_TEST_SUITE_P(Delivery, ExclusiveFullscreen, testing::Values("planar", "nscodec"));

TEST_F(FullscreenSample, ExplicitFullscreenKeepsDeclaredAspect) {
  ASSERT_NO_FATAL_FAILURE(GivenDesktopProcess({ }, { "--fullscreen", "--mode", "320x200", "--aspect", "4:3" }));
  auto& client = SessionClient();
  ASSERT_NO_FATAL_FAILURE(ThenExplicitGeometry(client, 240));
  SDL_Log("trace explicit aspect=4:3 window=320x200 gdi=320x240");
  Escape(client);
}

TEST_F(FullscreenSample, AspectMapsMouse) {
  ASSERT_NO_FATAL_FAILURE(GivenProcess({ }, AspectOptions()));
  auto client = AnnouncedClient(1024, 768);
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_NO_FATAL_FAILURE(ThenAbsoluteAspectMouse(client));
  ASSERT_NO_FATAL_FAILURE(ThenRelativeAspectMouse(client));
  Escape(client);
}

namespace {
auto ThenRefilledWindow(Client& client, Headless::FrameObserver& observer, std::size_t& acknowledged,
                        std::size_t window) -> void {
  ASSERT_EQ(observer.Frames().size() - acknowledged, window);
  ASSERT_TRUE(observer.Ack());
  acknowledged = observer.Frames().size();
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() >= acknowledged + window; }));
}
auto FillSendWindow(Client& client, Headless::FrameObserver& observer) -> void {
  Expects(observer.Installed(), "frame observer is installed");
  auto window = freerdp_settings_get_uint32(client.Instance()->context->settings, FreeRDP_FrameAcknowledge);
  ASSERT_EQ(window, 2u);
  std::size_t acknowledged = 0;
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() >= window; }));
  for (std::size_t i = 0; i < 30; ++i) {
    ASSERT_NO_FATAL_FAILURE(ThenRefilledWindow(client, observer, acknowledged, window));
  }
}

}
TEST_F(FullscreenSample, SendWindowWithoutRefreshFeedback) {
  ASSERT_NO_FATAL_FAILURE(GivenProcess({ }, { "--tight" }));
  auto client = AnnouncedClient(640, 480);
  ASSERT_NO_FATAL_FAILURE(ConnectAcknowledging(client));
  Headless::FrameObserver observer(client);
  ASSERT_TRUE(ReadInput(client, "event FOCUS_GAINED "));
  auto mode = process->Transcript().rfind("event DISPLAY_CURRENT_MODE_CHANGED ");
  ASSERT_NO_FATAL_FAILURE(FillSendWindow(client, observer));
  ASSERT_NO_FATAL_FAILURE(WhenKeyDown(client));
  EXPECT_EQ(process->Transcript().rfind("event DISPLAY_CURRENT_MODE_CHANGED "), mode);
  Escape(client);
}

}
}
