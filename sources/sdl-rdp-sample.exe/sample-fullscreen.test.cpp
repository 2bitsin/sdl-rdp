#include "_detail/sample-fixture.hpp"

namespace SampleGate {
namespace {
class FullscreenSample : public SampleGate::Sample {
protected:
  static void ThenWindowedPicture(Client& client) {
    ASSERT_TRUE(client.Until([&] {
      auto gdi = client.Instance()->context->gdi;
      return gdi->width == 640 && gdi->height == 480;
    }));
  }
  void ThenRelativeMotion(Client& client, rdpInput* input) {
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 330, 192));
    ASSERT_TRUE(ReadInput(client, "event MOUSE_MOTION "));
    EXPECT_TRUE(line.contains(" xrel=10 yrel=-35 ")) << line;
  }
  void ThenRelativeAspectMouse(Client& client) {
    auto* input = client.Instance()->context->input;
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 320, 240));
    ASSERT_TRUE(Read("event MOUSE_MOTION "));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3d));
    ASSERT_TRUE(Read("event RELATIVE_MODE active=1"));
    ThenRelativeMotion(client, input);
  }
  void ThenAbsoluteAspectMouse(Client& client) {
    ASSERT_TRUE(Read("event FOCUS_GAINED "));
    ThenWindowedPicture(client);
    if (::testing::Test::HasFatalFailure()) return;
    ASSERT_TRUE(freerdp_input_send_mouse_event(client.Instance()->context->input, PTR_FLAGS_MOVE, 639, 479));
    ASSERT_TRUE(Read("event MOUSE_MOTION "));
    EXPECT_TRUE(line.contains(" x=639 y=349 ")) << line;
  }
  void ThenWindowedGeometry(Client& client) {
    ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=640x480"));
    ThenWindowedPicture(client);
  }
  static void ThenExclusiveSize(FirstFrameSize const& frame) {
    EXPECT_EQ(frame.Width(), 320);
    EXPECT_EQ(frame.Height(), 200);
  }
};
TEST_F(FullscreenSample, ExplicitFullscreenBeforeConnect) {
  GivenFullscreen();
  if (::testing::Test::HasFatalFailure()) return;
  auto port = Number(std::string_view(line).substr(5));
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  Client         client(port, true, 1280, 800);
  FirstFrameSize frame(client);
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  ASSERT_TRUE(client.Until([&] { return frame.Received(); }));
  ThenExclusiveSize(frame);
  if (::testing::Test::HasFatalFailure()) return;
  Escape(client);
}

TEST_F(FullscreenSample, ExplicitFullscreenRestoresWindow) {
  GivenFullscreen();
  if (::testing::Test::HasFatalFailure()) return;
  Client client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  ConnectExposed(client);
  if (::testing::Test::HasFatalFailure()) return;
  ThenExplicitGeometry(client);
  if (::testing::Test::HasFatalFailure()) return;
  SDL_Log("trace explicit screen=1280x800 gdi=320x200");
  PressFullscreenKey(client);
  if (::testing::Test::HasFatalFailure()) return;
  ThenWindowedGeometry(client);
  if (::testing::Test::HasFatalFailure()) return;
  SDL_Log("trace leave gdi=640x480 screen=1280x800");
  PressFullscreenKey(client);
  if (::testing::Test::HasFatalFailure()) return;
  ThenExplicitGeometry(client);
  if (::testing::Test::HasFatalFailure()) return;
  SDL_Log("trace reenter gdi=320x200");
  Escape(client);
}

TEST_F(FullscreenSample, ExplicitFullscreenSurvivesScreenChange) {
  GivenFullscreen();
  if (::testing::Test::HasFatalFailure()) return;
  Client                        client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  Headless::DisplayClient const display(client);
  ConnectExposed(client);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ChangeMonitor(client);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(ReadInput(
      client, "event DISPLAY_DESKTOP_MODE_CHANGED type=" + std::to_string(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED) +
                  " width=1920 height=1080"));
  ThenExplicitGeometry(client);
  if (::testing::Test::HasFatalFailure()) return;
  SDL_Log("trace screen=1920x1080 gdi=320x200");
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x3e));
  ThenWindowedGeometry(client);
  if (::testing::Test::HasFatalFailure()) return;
  SDL_Log("trace leave gdi=640x480 screen=1920x1080");
  Escape(client);
}

class RefreshMode : public FullscreenSample, public testing::WithParamInterface<char const*> {
protected:
  void ThenDeclaredRate() {
    std::string_view const kind = GetParam();
    auto const*            rate = kind == "exclusive" ? "90" : kind == "borderless" ? "75" : "60";
    EXPECT_TRUE(
        process->Transcript().contains(std::string("refresh=") + rate + " numerator=" + rate + " denominator=1"));
  }
  void WhenFocusSynchronized(Client& client) {
    ASSERT_TRUE(ReadInput(client, "event FOCUS_GAINED "));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x30));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_RELEASE, 0x30));
    ASSERT_TRUE(ReadInput(client, "event KEY_UP "));
  }
  unsigned Count(std::string_view event) {
    Expects(!event.empty(), "event name is supplied");
    unsigned total = 0;
    for (std::size_t at = 0; (at = process->Transcript().find(event, at)) != std::string::npos; at += event.size())
      ++total;
    return total;
  }
  void Start() {
    Expects(process == nullptr, "sample has not started");
    auto arguments = Arguments(certificates.Path(), false);
    arguments.insert(arguments.end() - 1, { "SDL_RDP_WIDTH=640", "SDL_RDP_HEIGHT=480" });
    std::string_view const kind = GetParam();
    if (kind == "borderless") arguments.insert(arguments.end() - 1, "SDL_RDP_REFRESH=75");
    if (kind == "exclusive") {
      auto ini = certificates.Path() / "refresh.ini";
      {
        std::ofstream file(ini);
        file << "SDL_RDP_REFRESH=90\n";
      }
      arguments.insert(arguments.end() - 1, "SDL_RDP_INI=" + ini.string());
    }
    arguments.emplace_back("--tight");
    if (kind != "windowed") arguments.emplace_back("--fullscreen");
    if (kind == "exclusive") arguments.insert(arguments.end(), { "--mode", "320x200" });
    process = std::make_unique<Process>(arguments);
    ASSERT_TRUE(Read("port "));
  }
  static void AcknowledgeCadence(Client& client, Headless::FrameObserver& frames) {
    for (unsigned i = 0; i < 6; ++i) {
      auto before = frames.Frames().size();
      std::this_thread::sleep_for(i % 2 ? 20ms : 120ms);
      ASSERT_TRUE(frames.Ack());
      ASSERT_TRUE(client.Until([&] { return frames.Frames().size() >= before + 2; }));
    }
  }
  void Observe(Client& client, Headless::FrameObserver& frames) {
    Expects(process != nullptr, "sample is running");
    WhenFocusSynchronized(client);
    if (::testing::Test::HasFatalFailure()) return;
    auto desktop = Count("event DISPLAY_DESKTOP_MODE_CHANGED ");
    auto current = Count("event DISPLAY_CURRENT_MODE_CHANGED ");
    ThenDeclaredRate();
    if (::testing::Test::HasFatalFailure()) return;
    AcknowledgeCadence(client, frames);
    if (::testing::Test::HasFatalFailure()) return;
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x1e));
    ASSERT_TRUE(ReadInput(client, "event KEY_DOWN "));
    EXPECT_EQ(Count("event DISPLAY_CURRENT_MODE_CHANGED "), current);
    EXPECT_EQ(Count("event DISPLAY_DESKTOP_MODE_CHANGED "), desktop);
  }
};

TEST_P(RefreshMode, AcknowledgementsPreserveDeclaredRate) {
  Expects(process == nullptr, "sample has not started");
  Start();
  if (::testing::Test::HasFatalFailure()) return;
  Client client(Number(std::string_view(line).substr(5)), true, 1024, 768);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_FrameAcknowledge, 2));
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  Headless::FrameObserver frames(client);
  ASSERT_TRUE(client.Until([&] { return !frames.Frames().empty(); }));
  ASSERT_TRUE(frames.Ack());
  Observe(client, frames);
  if (::testing::Test::HasFatalFailure()) return;
  Escape(client);
}

INSTANTIATE_TEST_SUITE_P(Window, RefreshMode, testing::Values("windowed", "borderless", "exclusive"));

class ExclusiveFullscreen : public FullscreenSample, public testing::WithParamInterface<char const*> {
protected:
  void WhenDesktopModeChanges(Client& client, Headless::DisplayClient& display, FullDesktopFrames const& desktop) {
    ASSERT_GT(desktop.Full(), 0u);
    ASSERT_TRUE(display.Layout(1920, 1080));
    ASSERT_TRUE(ReadInput(
        client, "event DISPLAY_DESKTOP_MODE_CHANGED type=" + std::to_string(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED) +
                    " width=1920 height=1080"));
  }
  static void ThenNextWindow(Client& client, Headless::FrameObserver& frames) {
    auto initial = frames.Frames().size();
    ASSERT_TRUE(frames.Ack());
    ASSERT_TRUE(client.Until([&] { return frames.Frames().size() >= initial + 2; }));
  }
  static void ThenIncrementalPicture(Client& client, FullDesktopFrames const& desktop, unsigned baseline,
                                     unsigned deliveries) {
    EXPECT_EQ(desktop.Full(), baseline);
    EXPECT_GT(desktop.Deliveries(), deliveries);
    EXPECT_EQ(client.Instance()->context->gdi->width, 320);
    EXPECT_EQ(client.Instance()->context->gdi->height, 200);
  }
  void DelayAcknowledgement(Client& client, Headless::FrameObserver& frames) {
    Expects(frames.Acknowledgements().size() >= 2, "two previous acknowledgements define the delay");
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x1e));
    ASSERT_TRUE(ReadInput(client, "event KEY_DOWN "));
    auto interval = frames.Acknowledgements().back() - frames.Acknowledgements()[frames.Acknowledgements().size() - 2];
    std::this_thread::sleep_until(frames.Acknowledgements().back() + (interval * 3));
  }
  void IncrementalFrames(Client& client, Headless::FrameObserver& frames, FullDesktopFrames& desktop,
                         std::string_view change) {
    Expects(!change.empty(), "frame trigger is named");
    auto baseline   = desktop.Full();
    auto before     = frames.Frames().size();
    auto deliveries = desktop.Deliveries();
    if (change == "delayed ack") {
      DelayAcknowledgement(client, frames);
      if (::testing::Test::HasFatalFailure()) return;
    }
    ASSERT_TRUE(frames.Ack());
    ASSERT_TRUE(client.Until([&] { return frames.Frames().size() >= before + 2; }));
    ThenIncrementalPicture(client, desktop, baseline, deliveries);
    if (::testing::Test::HasFatalFailure()) return;
    SDL_Log("trace exclusive %.*s gdi=%dx%d new_full_desktop=%u frames=%zu", int(change.size()), change.data(),
            client.Instance()->context->gdi->width, client.Instance()->context->gdi->height, desktop.Full() - baseline,
            frames.Frames().size() - before);
  }
  void Start() {
    Expects(process == nullptr, "sample has not started");
    auto arguments = Arguments(certificates.Path(), false);
    *std::ranges::find(arguments, std::string("SDL_RDP_CODEC=planar")) = "SDL_RDP_CODEC=" + std::string(GetParam());
    arguments.insert(arguments.end(), { "--fullscreen", "--mode", "320x200", "--partial" });
    process = std::make_unique<Process>(arguments);
    ASSERT_TRUE(Read("port "));
  }
  void InitialFrames(Client& client, Headless::DisplayClient& /*display*/, Headless::FrameObserver& frames) {
    Expects(process != nullptr, "sample is running");
    ASSERT_TRUE(client.Until([&] { return !frames.Frames().empty(); }));
    auto initial = frames.Frames().size();
    ASSERT_TRUE(frames.Ack());
    ASSERT_TRUE(ReadInput(client, "event FOCUS_GAINED "));
    ASSERT_TRUE(
        client.Until([&] { return Headless::DisplayClient::Ready() && frames.Frames().size() >= initial + 2; }));
    ThenNextWindow(client, frames);
  }
};

TEST_P(ExclusiveFullscreen, DoesNotRepaintOnModeChanges) {
  Expects(process == nullptr, "sample has not started");
  Start();
  if (::testing::Test::HasFatalFailure()) return;
  Client                  client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  Headless::DisplayClient display(client);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_FrameAcknowledge, 2));
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  FullDesktopFrames       desktop(client);
  Headless::FrameObserver frames(client);
  InitialFrames(client, display, frames);
  if (::testing::Test::HasFatalFailure()) return;
  WhenDesktopModeChanges(client, display, desktop);
  if (::testing::Test::HasFatalFailure()) return;
  IncrementalFrames(client, frames, desktop, "screen");
  if (::testing::Test::HasFatalFailure()) return;
  IncrementalFrames(client, frames, desktop, "delayed ack");
  if (::testing::Test::HasFatalFailure()) return;
  Escape(client);
}

INSTANTIATE_TEST_SUITE_P(Delivery, ExclusiveFullscreen, testing::Values("planar", "nscodec"));

TEST_F(FullscreenSample, ExplicitFullscreenKeepsDeclaredAspect) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--fullscreen", "--mode", "320x200", "--aspect", "4:3" });
  GivenDesktopProcess(arguments);
  if (::testing::Test::HasFatalFailure()) return;
  auto& client = SessionClient();
  ThenExplicitGeometry(client, 240);
  if (::testing::Test::HasFatalFailure()) return;
  SDL_Log("trace explicit aspect=4:3 window=320x200 gdi=320x240");
  Escape(client);
}

TEST_F(FullscreenSample, AspectMapsMouse) {
  GivenAspect();
  if (::testing::Test::HasFatalFailure()) return;
  Client client(Number(std::string_view(line).substr(5)), true, 1024, 768);
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs();
  ThenAbsoluteAspectMouse(client);
  if (::testing::Test::HasFatalFailure()) return;
  ThenRelativeAspectMouse(client);
  if (::testing::Test::HasFatalFailure()) return;
  Escape(client);
}

namespace {
void ThenRefilledWindow(Client& client, Headless::FrameObserver& observer, std::size_t& acknowledged, unsigned window) {
  ASSERT_EQ(observer.Frames().size() - acknowledged, window);
  ASSERT_TRUE(observer.Ack());
  acknowledged = observer.Frames().size();
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() >= acknowledged + window; }));
}
void FillSendWindow(Client& client, Headless::FrameObserver& observer) {
  Expects(observer.Installed(), "frame observer is installed");
  auto window = freerdp_settings_get_uint32(client.Instance()->context->settings, FreeRDP_FrameAcknowledge);
  ASSERT_EQ(window, 2u);
  std::size_t acknowledged = 0;
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() >= window; }));
  for (unsigned i = 0; i < 30; ++i) {
    ThenRefilledWindow(client, observer, acknowledged, window);
    if (::testing::Test::HasFatalFailure()) return;
  }
}

}
TEST_F(FullscreenSample, SendWindowWithoutRefreshFeedback) {
  Expects(process == nullptr, "sample has not started");
  auto arguments = Arguments(certificates.Path(), false);
  arguments.emplace_back("--tight");
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_FrameAcknowledge, 2));
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs();
  Headless::FrameObserver observer(client);
  ASSERT_TRUE(ReadInput(client, "event FOCUS_GAINED "));
  auto mode = process->Transcript().rfind("event DISPLAY_CURRENT_MODE_CHANGED ");
  FillSendWindow(client, observer);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x1e));
  ASSERT_TRUE(ReadInput(client, "event KEY_DOWN "));
  EXPECT_EQ(process->Transcript().rfind("event DISPLAY_CURRENT_MODE_CHANGED "), mode);
  Escape(client);
}

}
}
