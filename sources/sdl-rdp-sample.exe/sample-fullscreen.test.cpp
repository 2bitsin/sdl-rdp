#include <sdl-rdp-backend.so/_detail/avc.hpp>
#include "_detail/sample-fixture.hpp"
#include <sdl-rdp-backend.so/_detail/headless-clipboard.hpp>
#include <sdl-rdp-backend.so/_detail/headless-audio.hpp>
#include <sdl-rdp-backend.so/_detail/headless-tls.hpp>
#include <sdl-rdp-backend.so/_detail/headless-drive.hpp>
#include <cmath>

namespace SampleGate {
TEST_F(Sample, ExplicitFullscreenBeforeConnect) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--fullscreen", "--mode", "320x200"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  auto port = Number(std::string_view(line).substr(5));
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  Client client(port, true, 1280, 800);
  FirstFrameSize frame(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return frame.received; }));
  EXPECT_EQ(frame.width, 320);
  EXPECT_EQ(frame.height, 200);
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, ExplicitFullscreenRestoresWindow) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--fullscreen", "--mode", "320x200"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 320 && gdi->height == 200; }));
  SDL_Log("trace explicit screen=1280x800 gdi=320x200");
  auto input = client.instance->context->input;
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

TEST_F(Sample, ExplicitFullscreenSurvivesScreenChange) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--fullscreen", "--mode", "320x200"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  Headless::DisplayClient display(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ASSERT_TRUE(client.Until([&] { return display.ready.load(); }));
  DISPLAY_CONTROL_MONITOR_LAYOUT monitor{};
  monitor.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
  monitor.Width = 1920; monitor.Height = 1080;
  monitor.PhysicalWidth = 500; monitor.PhysicalHeight = 300;
  monitor.DesktopScaleFactor = monitor.DeviceScaleFactor = 100;
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

class RefreshMode : public Sample, public testing::WithParamInterface<const char*> {};

TEST_P(RefreshMode, EstimatesOnlyChangeCurrentMode) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end() - 1, {"SDL_RDP_WIDTH=640", "SDL_RDP_HEIGHT=480"});
  arguments.push_back("--tight");
  std::string_view kind = GetParam();
  if (kind != "windowed") arguments.push_back("--fullscreen");
  if (kind == "exclusive") arguments.insert(arguments.end(), {"--mode", "320x200"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 1024, 768);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge, 2));
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  Headless::FrameObserver frames(client);
  ASSERT_TRUE(client.Until([&] { return !frames.ids.empty(); }));
  ASSERT_TRUE(frames.Ack());
  ASSERT_TRUE(ReadInput(client, "event FOCUS_GAINED "));
  auto count = [&](std::string_view event) {
    unsigned total = 0;
    for (std::size_t at = 0; (at = process->transcript.find(event, at)) != std::string::npos; at += event.size()) ++total;
    return total;
  };
  auto desktop = count("event DISPLAY_DESKTOP_MODE_CHANGED ");
  ASSERT_EQ(desktop, kind == "exclusive" ? 2u : 1u) << process->transcript;
  auto current = count("event DISPLAY_CURRENT_MODE_CHANGED ");
  unsigned estimates = 0, acknowledgements = 0;
  auto next_ack = Clock::now();
  ASSERT_TRUE(client.Until([&] {
    if (!frames.ids.empty() && Clock::now() >= next_ack) {
      EXPECT_TRUE(frames.Ack());
      next_ack = Clock::now() + (++acknowledgements % 2 ? 20ms : 120ms);
    }
    while (process->Line(line, Clock::now() + 1ms)) {
      if (!line.starts_with("event DISPLAY_CURRENT_MODE_CHANGED ")) continue;
      ++estimates;
      EXPECT_FALSE(line.contains("numerator=0 ")) << line;
      EXPECT_TRUE(line.ends_with(kind == "exclusive" ? " width=320 height=200" : " width=1024 height=768")) << line;
    }
    return estimates >= 5;
  })) << process->transcript;
  // An input barrier drains all mode events preceding the final acknowledgement.
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x1e));
  ASSERT_TRUE(ReadInput(client, "event KEY_DOWN "));
  EXPECT_GE(count("event DISPLAY_CURRENT_MODE_CHANGED "), current + 5);
  EXPECT_EQ(count("event DISPLAY_DESKTOP_MODE_CHANGED "), desktop) << process->transcript;
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

INSTANTIATE_TEST_SUITE_P(Window, RefreshMode, testing::Values("windowed", "borderless", "exclusive"));

class ExclusiveFullscreen : public Sample, public testing::WithParamInterface<const char*> {};

TEST_P(ExclusiveFullscreen, DoesNotRepaintOnModeChanges) {
  auto arguments = Arguments(certificates.Path(), false);
  *std::ranges::find(arguments, std::string("SDL_RDP_CODEC=planar")) = "SDL_RDP_CODEC=" + std::string(GetParam());
  arguments.insert(arguments.end(), {"--fullscreen", "--mode", "320x200", "--partial"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  Headless::DisplayClient display(client);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge, 2));
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  FullDesktopFrames desktop(client);
  Headless::FrameObserver frames(client);
  ASSERT_TRUE(client.Until([&] { return !frames.ids.empty(); }));
  auto initial = frames.ids.size();
  ASSERT_TRUE(frames.Ack());
  ASSERT_TRUE(ReadInput(client, "event FOCUS_GAINED "));
  ASSERT_TRUE(client.Until([&] { return display.ready.load() && frames.ids.size() >= initial + 2; }));
  initial = frames.ids.size();
  ASSERT_TRUE(frames.Ack());
  ASSERT_TRUE(client.Until([&] { return frames.ids.size() >= initial + 2; }));
  ASSERT_GT(desktop.full, 0u);
  DISPLAY_CONTROL_MONITOR_LAYOUT monitor{};
  monitor.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
  monitor.Width = 1920; monitor.Height = 1080;
  monitor.PhysicalWidth = 500; monitor.PhysicalHeight = 300;
  monitor.DesktopScaleFactor = monitor.DeviceScaleFactor = 100;
  ASSERT_EQ(display.channel.load()->SendMonitorLayout(display.channel.load(), 1, &monitor), CHANNEL_RC_OK);
  ASSERT_TRUE(ReadInput(client, "event DISPLAY_DESKTOP_MODE_CHANGED type=" + std::to_string(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED) + " width=1920 height=1080"));
  ASSERT_NO_FATAL_FAILURE(IncrementalFrames(client, frames, desktop, "screen"));
  ASSERT_NO_FATAL_FAILURE(IncrementalFrames(client, frames, desktop, "refresh"));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

INSTANTIATE_TEST_SUITE_P(Delivery, ExclusiveFullscreen, testing::Values("planar", "nscodec"));

TEST_F(Sample, ExplicitFullscreenKeepsDeclaredAspect) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--fullscreen", "--mode", "320x200", "--aspect", "4:3"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 1280, 800);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_NO_FATAL_FAILURE(Exposed());
  ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 320 && gdi->height == 240; }));
  SDL_Log("trace explicit aspect=4:3 window=320x200 gdi=320x240");
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, AspectMapsMouse) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--size", "640x350", "--aspect", "4:3"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 1024, 768);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
  ASSERT_TRUE(client.Until([&] { auto gdi = client.instance->context->gdi; return gdi->width == 640 && gdi->height == 480; }));
  ASSERT_TRUE(freerdp_input_send_mouse_event(client.instance->context->input, PTR_FLAGS_MOVE, 639, 479));
  ASSERT_TRUE(Read("event MOUSE_MOTION "));
  EXPECT_TRUE(line.contains(" x=639 y=349 ")) << line;
  auto input = client.instance->context->input;
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 320, 240));
  ASSERT_TRUE(Read("event MOUSE_MOTION "));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3d));
  ASSERT_TRUE(Read("event RELATIVE_MODE active=1"));
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 330, 192));
  ASSERT_TRUE(ReadInput(client, "event MOUSE_MOTION "));
  EXPECT_TRUE(line.contains(" xrel=10 yrel=-35 ")) << line;
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, VsyncAndRefresh) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.push_back("--tight");
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge, 2));
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << ConnectLogs();
  Headless::FrameObserver observer(client);
  auto window = freerdp_settings_get_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge);
  ASSERT_EQ(window, 2u);
  std::size_t acknowledged = 0;
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() >= window; }));
  for (unsigned i = 0; i < 30; ++i) {
    ASSERT_EQ(observer.ids.size() - acknowledged, window);
    ASSERT_TRUE(observer.Ack());
    acknowledged = observer.ids.size();
    ASSERT_TRUE(client.Until([&] { return observer.ids.size() >= acknowledged + window; }));
    // The full negotiated window was outstanding before this ACK. The next
    // frame could only be sent after the server processed it.
    observer.ack_processed.push_back(observer.received[acknowledged]);
  }
  // A key is an ordered barrier through SDL's event queue. Collect mode changes
  // through that barrier instead of assuming a quiet 150 ms means the last one.
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x1e));
  double rate = 0;
  bool barrier = false;
  ASSERT_TRUE(client.Until([&] {
    while (process->Line(line, Clock::now() + 1ms)) {
      if (line.starts_with("event DISPLAY_CURRENT_MODE_CHANGED ")) {
        auto position = line.find(" refresh=");
        if (position != std::string::npos) rate = std::stod(line.substr(position + 9));
      }
      if (line.starts_with("event KEY_DOWN ")) { barrier = true; break; }
    }
    return barrier;
  }));
  auto [low, high] = observer.RefreshBounds();
  EXPECT_GE(rate, low);
  EXPECT_LE(rate, high);
  SDL_Log("event PACING frames=%zu rate=%.3f measured_bounds=%.3f..%.3f",
          observer.ids.size(), rate, low, high);
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}
}
