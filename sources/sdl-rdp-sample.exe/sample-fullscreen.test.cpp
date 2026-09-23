#include "_detail/sample-fixture.hpp"

namespace SampleGate {
TEST_F(Sample, ExplicitFullscreenBeforeConnect)
{
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--fullscreen", "--mode", "320x200" });
  ASSERT_NO_FATAL_FAILURE(GivenProcess(arguments));
  auto port = Number(std::string_view(line).substr(5));
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  Client         client(port, true, 1280, 800);
  FirstFrameSize frame(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return frame.received; }));
  EXPECT_EQ(frame.width, 320);
  EXPECT_EQ(frame.height, 200);
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, ExplicitFullscreenRestoresWindow)
{
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--fullscreen", "--mode", "320x200" });
  ASSERT_NO_FATAL_FAILURE(GivenProcess(arguments));
  Client client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 320 && gdi->height == 200; }));
  SDL_Log("trace explicit screen=1280x800 gdi=320x200");
  auto* input = client.instance->context->input;
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3e));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3e));
  ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=640x480"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 640 && gdi->height == 480; }));
  SDL_Log("trace leave gdi=640x480 screen=1280x800");
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3e));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3e));
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 320 && gdi->height == 200; }));
  SDL_Log("trace reenter gdi=320x200");
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, ExplicitFullscreenSurvivesScreenChange)
{
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--fullscreen", "--mode", "320x200" });
  ASSERT_NO_FATAL_FAILURE(GivenProcess(arguments));
  Client                  client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  Headless::DisplayClient display(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ASSERT_TRUE(client.Until([&] { return display.ready.load(); }));
  auto monitor = Headless::DisplayClient::Monitor(1920, 1080, 500);
  ASSERT_EQ(display.channel.load()->SendMonitorLayout(display.channel.load(), 1, &monitor), CHANNEL_RC_OK);
  ASSERT_TRUE(ReadInput(client, "event DISPLAY_DESKTOP_MODE_CHANGED type=" + std::to_string(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED) + " width=1920 height=1080"));
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 320 && gdi->height == 200; }));
  SDL_Log("trace screen=1920x1080 gdi=320x200");
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x3e));
  ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=640x480"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 640 && gdi->height == 480; }));
  SDL_Log("trace leave gdi=640x480 screen=1920x1080");
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

class RefreshMode : public Sample, public testing::WithParamInterface<const char*> {
protected:
  unsigned Count(std::string_view event) {
    Expects(!event.empty(), "event name is supplied");
    unsigned total = 0;
    for (std::size_t at = 0; (at = process->transcript.find(event, at)) != std::string::npos; at += event.size()) ++total;
    return total;
  }
  void Start() {
    Expects(process == nullptr, "sample has not started");
    auto arguments = Arguments(certificates.Path(), false);
    arguments.insert(arguments.end() - 1, {"SDL_RDP_WIDTH=640", "SDL_RDP_HEIGHT=480"});
    std::string_view kind = GetParam();
    if (kind == "borderless") arguments.insert(arguments.end() - 1, "SDL_RDP_REFRESH=75");
    if (kind == "exclusive") {
      auto ini = certificates.Path() / "refresh.ini";
      { std::ofstream file(ini); file << "SDL_RDP_REFRESH=90\n"; }
      arguments.insert(arguments.end() - 1, "SDL_RDP_INI=" + ini.string());
    }
    arguments.push_back("--tight");
    if (kind != "windowed") arguments.push_back("--fullscreen");
    if (kind == "exclusive") arguments.insert(arguments.end(), {"--mode", "320x200"});
    process = std::make_unique<Process>(arguments);
    ASSERT_TRUE(Read("port "));
  }
  void Observe(Client& client, Headless::FrameObserver& frames) {
    Expects(process != nullptr, "sample is running");
    ASSERT_TRUE(ReadInput(client, "event FOCUS_GAINED "));
    auto desktop = Count("event DISPLAY_DESKTOP_MODE_CHANGED ");
    auto current = Count("event DISPLAY_CURRENT_MODE_CHANGED ");
    std::string_view kind = GetParam();
    auto rate = kind == "exclusive" ? "90" : kind == "borderless" ? "75" : "60";
    EXPECT_TRUE(process->transcript.contains(std::string("refresh=") + rate + " numerator=" + rate + " denominator=1"));
    for (unsigned i = 0; i < 6; ++i) {
      auto before = frames.ids.size();
      std::this_thread::sleep_for(i % 2 ? 20ms : 120ms);
      ASSERT_TRUE(frames.Ack());
      ASSERT_TRUE(client.Until([&] { return frames.ids.size() >= before + 2; }));
    }
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x1e));
    ASSERT_TRUE(ReadInput(client, "event KEY_DOWN "));
    EXPECT_EQ(Count("event DISPLAY_CURRENT_MODE_CHANGED "), current);
    EXPECT_EQ(Count("event DISPLAY_DESKTOP_MODE_CHANGED "), desktop);
  }
};

TEST_P(RefreshMode, AcknowledgementsPreserveDeclaredRate) {
  Expects(process == nullptr, "sample has not started");
  ASSERT_NO_FATAL_FAILURE(Start());
  Client client(Number(std::string_view(line).substr(5)), true, 1024, 768);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge, 2));
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  Headless::FrameObserver frames(client);
  ASSERT_TRUE(client.Until([&] { return !frames.ids.empty(); }));
  ASSERT_TRUE(frames.Ack());
  ASSERT_NO_FATAL_FAILURE(Observe(client, frames));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

INSTANTIATE_TEST_SUITE_P(Window, RefreshMode, testing::Values("windowed", "borderless", "exclusive"));

class ExclusiveFullscreen : public Sample, public testing::WithParamInterface<const char*> {
protected:
  void Start() {
    Expects(process == nullptr, "sample has not started");
    auto arguments = Arguments(certificates.Path(), false);
    *std::ranges::find(arguments, std::string("SDL_RDP_CODEC=planar")) = "SDL_RDP_CODEC=" + std::string(GetParam());
    arguments.insert(arguments.end(), {"--fullscreen", "--mode", "320x200", "--partial"});
    process = std::make_unique<Process>(arguments);
    ASSERT_TRUE(Read("port "));
  }
  void InitialFrames(Client& client, Headless::DisplayClient& display, Headless::FrameObserver& frames) {
    Expects(process != nullptr, "sample is running");
    ASSERT_TRUE(client.Until([&] { return !frames.ids.empty(); }));
    auto initial = frames.ids.size();
    ASSERT_TRUE(frames.Ack());
    ASSERT_TRUE(ReadInput(client, "event FOCUS_GAINED "));
    ASSERT_TRUE(client.Until([&] { return display.ready.load() && frames.ids.size() >= initial + 2; }));
    initial = frames.ids.size();
    ASSERT_TRUE(frames.Ack());
    ASSERT_TRUE(client.Until([&] { return frames.ids.size() >= initial + 2; }));
  }
};

TEST_P(ExclusiveFullscreen, DoesNotRepaintOnModeChanges) {
  Expects(process == nullptr, "sample has not started");
  ASSERT_NO_FATAL_FAILURE(Start());
  Client client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  Headless::DisplayClient display(client);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge, 2));
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  FullDesktopFrames desktop(client);
  Headless::FrameObserver frames(client);
  ASSERT_NO_FATAL_FAILURE(InitialFrames(client, display, frames));
  ASSERT_GT(desktop.full, 0u);
  ASSERT_TRUE(display.Layout(1920, 1080));
  ASSERT_TRUE(ReadInput(client, "event DISPLAY_DESKTOP_MODE_CHANGED type=" + std::to_string(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED) + " width=1920 height=1080"));
  ASSERT_NO_FATAL_FAILURE(IncrementalFrames(client, frames, desktop, "screen"));
  ASSERT_NO_FATAL_FAILURE(IncrementalFrames(client, frames, desktop, "delayed ack"));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

INSTANTIATE_TEST_SUITE_P(Delivery, ExclusiveFullscreen, testing::Values("planar", "nscodec"));

TEST_F(Sample, ExplicitFullscreenKeepsDeclaredAspect)
{
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--fullscreen", "--mode", "320x200", "--aspect", "4:3" });
  ASSERT_NO_FATAL_FAILURE(GivenProcess(arguments));
  Client client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 320 && gdi->height == 240; }));
  SDL_Log("trace explicit aspect=4:3 window=320x200 gdi=320x240");
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, AspectMapsMouse)
{
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), { "--size", "640x350", "--aspect", "4:3" });
  ASSERT_NO_FATAL_FAILURE(GivenProcess(arguments));
  Client client(Number(std::string_view(line).substr(5)), true, 1024, 768);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 640 && gdi->height == 480; }));
  ASSERT_TRUE(freerdp_input_send_mouse_event(client.instance->context->input, PTR_FLAGS_MOVE, 639, 479));
  ASSERT_TRUE(Read("event MOUSE_MOTION "));
  EXPECT_TRUE(line.contains(" x=639 y=349 ")) << line;
  auto* input = client.instance->context->input;
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 320, 240));
  ASSERT_TRUE(Read("event MOUSE_MOTION "));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3d));
  ASSERT_TRUE(Read("event RELATIVE_MODE active=1"));
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 330, 192));
  ASSERT_TRUE(ReadInput(client, "event MOUSE_MOTION "));
  EXPECT_TRUE(line.contains(" xrel=10 yrel=-35 ")) << line;
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

static void FillSendWindow(Client& client, Headless::FrameObserver& observer) {
  Expects(observer.update != nullptr, "frame observer is installed");
  auto window = freerdp_settings_get_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge);
  ASSERT_EQ(window, 2u);
  std::size_t acknowledged = 0;
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() >= window; }));
  for (unsigned i = 0; i < 30; ++i) {
    ASSERT_EQ(observer.ids.size() - acknowledged, window);
    ASSERT_TRUE(observer.Ack());
    acknowledged = observer.ids.size();
    ASSERT_TRUE(client.Until([&] { return observer.ids.size() >= acknowledged + window; }));
  }
}

TEST_F(Sample, SendWindowWithoutRefreshFeedback) {
  Expects(process == nullptr, "sample has not started");
  auto arguments = Arguments(certificates.Path(), false);
  arguments.push_back("--tight");
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge, 2));
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  Headless::FrameObserver observer(client);
  ASSERT_TRUE(ReadInput(client, "event FOCUS_GAINED "));
  auto mode = process->transcript.rfind("event DISPLAY_CURRENT_MODE_CHANGED ");
  ASSERT_NO_FATAL_FAILURE(FillSendWindow(client, observer));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x1e));
  ASSERT_TRUE(ReadInput(client, "event KEY_DOWN "));
  EXPECT_EQ(process->transcript.rfind("event DISPLAY_CURRENT_MODE_CHANGED "), mode);
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

}
