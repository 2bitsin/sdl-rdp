#include "support.test/sample-desktop-steps.hpp"

#include "support.test/client-steps.hpp"
#include "support.test/frame-pattern.hpp"
#include "support.test/sample-launch.hpp"

#include <SDL3/SDL.h>
#include <format>
#include <utility>

namespace SampleGate {
auto SampleDesktopSteps::GivenAdvancedSession() -> void {
  GivenInputSession(true);
  if (::testing::Test::HasFatalFailure()) return;
  ThenAdvanced(SessionClient());
}
auto SampleDesktopSteps::PressFullscreenKey(Client& client) -> void {
  auto* input = client.Instance()->context->input;
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3e));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3e));
}
auto SampleDesktopSteps::WhenUnicodeClipboardOffered(Client& client, std::vector<BYTE> const& bytes) -> void {
  ASSERT_EQ(ClipboardSession().Offer(bytes), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return ClipboardSession().Observed().requests.load() == 2; }));
  ASSERT_TRUE(Read("event CLIPBOARD text=żółw"));
  SDL_Log("trace CLIPBOARD client formats=13 request=13 utf16le=7c01f300420177000000 text=żółw");
}
auto SampleDesktopSteps::WhenClipboardEmptied(Client& client) -> void {
  ASSERT_EQ(ClipboardSession().Offer({ 0, 0 }), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return ClipboardSession().Observed().requests.load() == 1; }));
  ASSERT_TRUE(Read("event CLIPBOARD text="));
}
auto SampleDesktopSteps::WhenAsciiClipboardOffered(Client& client) -> void {
  ASSERT_EQ(ClipboardSession().Offer({ 'w', 0, 'o', 0, 'r', 0, 'l', 0, 'd', 0, 0, 0 }), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return ClipboardSession().Observed().requests.load() == 1; }));
  ASSERT_TRUE(Read("event CLIPBOARD text=world"));
  SDL_Log("trace CLIPBOARD client formats=13 request=13 utf16le=77006f0072006c0064000000 text=world");
}
auto SampleDesktopSteps::ThenSizeEvents(std::string const& dimensions) -> void {
  ASSERT_TRUE(Read("event RESIZED "));
  EXPECT_TRUE(line.ends_with(dimensions)) << line;
  ASSERT_TRUE(Read("event PIXEL_SIZE_CHANGED "));
  EXPECT_TRUE(line.ends_with(dimensions)) << line;
}
auto SampleDesktopSteps::ThenDesktopMode(Client& client, unsigned w, unsigned h) -> void {
  ASSERT_TRUE(ReadInput(
      client, "event DISPLAY_DESKTOP_MODE_CHANGED type=" + std::to_string(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED) +
                  std::format(" width={} height={}", w, h)));
  ASSERT_TRUE(Read(std::format("event GEOMETRY window={}x{} desktop={}x{}", w, h, w, h)));
  ASSERT_TRUE(client.Until([&] {
    auto* gdi = client.Instance()->context->gdi;
    return std::cmp_equal(gdi->width, w) && std::cmp_equal(gdi->height, h);
  }));
}
auto SampleDesktopSteps::ThenWaitingPort(unsigned port) -> void {
  ASSERT_TRUE(Read("port ")) << "port after connection: " << process->Transcript();
  ASSERT_EQ(AnnouncedPort(line), port) << line;
}
auto SampleDesktopSteps::WhenCodecKeyChanges(Client const& client) -> void {
  auto* input = client.Instance()->context->input;
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3b));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3b));
  ASSERT_TRUE(Read("event CODEC_CHANGED codec=nscodec")) << process->Transcript();
  SDL_Log("%s", process->Transcript().c_str());
}
auto SampleDesktopSteps::GivenSwitchableCodec(Client const& client) -> void {
  auto* settings = client.Instance()->context->settings;
  ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, TRUE));
  ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_NSCodec, TRUE));
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs();
}
auto SampleDesktopSteps::ThenTakeoverEvent(char const* expected) -> void {
  do {
    ASSERT_TRUE(process->Line(line, Clock::now() + 10s)) << process->Transcript();
  } while (!line.starts_with("event ") || line.starts_with("event GEOMETRY ") ||
           line.starts_with("event CONNECTED "));
  EXPECT_TRUE(line.starts_with("event " + std::string(expected) + " ")) << line;
}
auto SampleDesktopSteps::WhenSmallerDesktop(Client& first) -> void {
  ASSERT_TRUE(freerdp_connect(first.Instance().get())) << ConnectLogs();
  ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=320x200"));
  ASSERT_TRUE(first.Until([&] { return Pattern(first, false); }));
  ASSERT_TRUE(freerdp_disconnect(first.Instance().get()));
  ASSERT_TRUE(Read("event FOCUS_LOST "));
}
auto SampleDesktopSteps::WhenWholeSampleReconnects(Client const& client, unsigned port) -> void {
  ASSERT_TRUE(freerdp_disconnect(client.Instance().get())) << "disconnect";
  ASSERT_TRUE(Read("event OCCLUDED ")) << "OCCLUDED: " << process->Transcript();
  ASSERT_TRUE(Read("event FOCUS_LOST ")) << "FOCUS_LOST: " << process->Transcript();
  Client const second(port, true, 640, 480);
  ASSERT_TRUE(freerdp_connect(second.Instance().get())) << ConnectLogs() << "second session connects";
  Exposed();
  if (::testing::Test::HasFatalFailure()) return;
  Escape(second);
}
auto SampleDesktopSteps::GivenWholeSample() -> void {
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port ")) << "port <n>: " << process->Transcript();
}
}
