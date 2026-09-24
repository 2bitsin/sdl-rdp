#pragma once
#include "sample-checks.hpp"

namespace SampleGate {
inline void ConnectDrive(Client& client, fs::path const& share) {
  Headless::ShareDrive(client, share.c_str());
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
}

class SampleSession : public SampleChecks {
protected:
  void GivenDesktopProcess(std::vector<std::string> const& arguments) {
    GivenProcess(arguments);
    if (::testing::Test::HasFatalFailure()) return;
    session = std::make_unique<Client>(AnnouncedPort(line), true, 1280, 800);
    ConnectExposed(*session);
  }
  void GivenDriveProcess(std::vector<std::string> const& arguments, fs::path const& share) {
    GivenProcess(arguments);
    if (::testing::Test::HasFatalFailure()) return;
    session = std::make_unique<Client>(AnnouncedPort(line), true, 640, 480);
    ConnectDrive(*session, share);
  }

  void ConnectExposed(Client& client) {
    ASSERT_TRUE(freerdp_connect(client.Instance().get()));
    Exposed();
  }

  void GivenInputSession(bool advanced = false) {
    GivenProcess();
    if (::testing::Test::HasFatalFailure()) return;
    session = std::make_unique<Client>(AnnouncedPort(line), true, 640, 480);
    if (advanced) channels = std::make_unique<InputClient>(*session);
    GivenFocus(*session);
  }
  Client& SessionClient() { return *session; }
  void    GivenPositionSession() {
    GivenProcess();
    if (::testing::Test::HasFatalFailure()) return;
    session = std::make_unique<Client>(AnnouncedPort(line), true, 640, 480);
    ASSERT_TRUE(freerdp_connect(session->Instance().get()));
    position = std::make_unique<PositionObserver>(*session);
    ASSERT_TRUE(Read("event FOCUS_GAINED "));
  }
  PositionObserver& Position() { return *position; }
  void              GivenFullscreen() {
    auto arguments = Arguments(certificates.Path(), false);
    arguments.insert(arguments.end(), { "--fullscreen", "--mode", "320x200" });
    GivenProcess(arguments);
  }
  void GivenAspect() {
    auto arguments = Arguments(certificates.Path(), false);
    arguments.insert(arguments.end(), { "--size", "640x350", "--aspect", "4:3" });
    GivenProcess(arguments);
  }
  void ThenExplicitGeometry(Client& client, unsigned height = 200) {
    ASSERT_TRUE(Read("event GEOMETRY window=320x200 desktop=320x200"));
    ASSERT_TRUE(client.Until([&] {
      auto* gdi = client.Instance()->context->gdi;
      return gdi->width == 320 && std::cmp_equal(gdi->height, height);
    }));
  }
  void GivenAudioProcess(std::vector<std::string> const& arguments) {
    process = std::make_unique<Process>(arguments);
    ASSERT_TRUE(Read("port "));
    audio_port = AnnouncedPort(line);
    ASSERT_TRUE(Read("audio device=RDP client freq=44100"));
  }
  void ThenIniConnects(std::vector<std::string> const& args, unsigned port) {
    GivenIniProcess(args, port);
    if (::testing::Test::HasFatalFailure()) return;
    Client const client(port, true, 640, 480);
    ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs();
    Escape(client);
  }
  Headless::ClipboardClient& ClipboardSession() { return *clipboard; }
  void                       GivenClipboard(std::string const& text) {
    auto arguments = Arguments(certificates.Path(), false);
    arguments.insert(arguments.end(), { "--clip", text });
    GivenProcess(arguments);
    if (::testing::Test::HasFatalFailure()) return;
    session   = std::make_unique<Client>(AnnouncedPort(line), true, 640, 480);
    clipboard = std::make_unique<Headless::ClipboardClient>(*session);
    ASSERT_TRUE(freerdp_connect(session->Instance().get())) << ConnectLogs();
  }
  void GivenIniProcess(std::vector<std::string> const& args, unsigned port) {
    process = std::make_unique<Process>(args);
    ASSERT_TRUE(Read("port ")) << process->Transcript();
    EXPECT_EQ(AnnouncedPort(line), port);
  }
  unsigned audio_port = 0;

private:
  std::unique_ptr<Client>                    session;
  std::unique_ptr<Headless::ClipboardClient> clipboard;
  std::unique_ptr<InputClient>               channels;
  std::unique_ptr<PositionObserver>          position;
};
class SampleDesktopSteps : public SampleSession {
protected:
  void GivenAdvancedSession() {
    GivenInputSession(true);
    if (::testing::Test::HasFatalFailure()) return;
    ThenAdvanced(SessionClient());
  }
  static void PressFullscreenKey(Client& client) {
    auto* input = client.Instance()->context->input;
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3e));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3e));
  }

  void WhenUnicodeClipboardOffered(Client& client, std::vector<BYTE> const& bytes) {
    ASSERT_EQ(ClipboardSession().Offer(bytes), CHANNEL_RC_OK);
    ASSERT_TRUE(client.Until([&] { return ClipboardSession().Observed().requests.load() == 2; }));
    ASSERT_TRUE(Read("event CLIPBOARD text=żółw"));
    SDL_Log("trace CLIPBOARD client formats=13 request=13 utf16le=7c01f300420177000000 text=żółw");
  }
  void WhenClipboardEmptied(Client& client) {
    ASSERT_EQ(ClipboardSession().Offer({ 0, 0 }), CHANNEL_RC_OK);
    ASSERT_TRUE(client.Until([&] { return ClipboardSession().Observed().requests.load() == 1; }));
    ASSERT_TRUE(Read("event CLIPBOARD text="));
  }
  void WhenAsciiClipboardOffered(Client& client) {
    ASSERT_EQ(ClipboardSession().Offer({ 'w', 0, 'o', 0, 'r', 0, 'l', 0, 'd', 0, 0, 0 }), CHANNEL_RC_OK);
    ASSERT_TRUE(client.Until([&] { return ClipboardSession().Observed().requests.load() == 1; }));
    ASSERT_TRUE(Read("event CLIPBOARD text=world"));
    SDL_Log("trace CLIPBOARD client formats=13 request=13 utf16le=77006f0072006c0064000000 text=world");
  }
  void ThenSizeEvents(std::string const& dimensions) {
    ASSERT_TRUE(Read("event RESIZED "));
    EXPECT_TRUE(line.ends_with(dimensions)) << line;
    ASSERT_TRUE(Read("event PIXEL_SIZE_CHANGED "));
    EXPECT_TRUE(line.ends_with(dimensions)) << line;
  }
  void ThenDesktopMode(Client& client, unsigned w, unsigned h) {
    ASSERT_TRUE(ReadInput(
        client, "event DISPLAY_DESKTOP_MODE_CHANGED type=" + std::to_string(SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED) +
                    std::format(" width={} height={}", w, h)));
    ASSERT_TRUE(Read(std::format("event GEOMETRY window={}x{} desktop={}x{}", w, h, w, h)));
    ASSERT_TRUE(client.Until([&] {
      auto* gdi = client.Instance()->context->gdi;
      return std::cmp_equal(gdi->width, w) && std::cmp_equal(gdi->height, h);
    }));
  }
  void ThenWaitingPort(unsigned port) {
    ASSERT_TRUE(Read("port ")) << "port after connection: " << process->Transcript();
    ASSERT_EQ(AnnouncedPort(line), port) << line;
  }
  void WhenCodecKeyChanges(Client const& client) {
    auto* input = client.Instance()->context->input;
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3b));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3b));
    ASSERT_TRUE(Read("event CODEC_CHANGED codec=nscodec")) << process->Transcript();
    SDL_Log("%s", process->Transcript().c_str());
  }
  void GivenSwitchableCodec(Client const& client) {
    auto* settings = client.Instance()->context->settings;
    ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, TRUE));
    ASSERT_TRUE(freerdp_settings_set_bool(settings, FreeRDP_NSCodec, TRUE));
    ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs();
  }
  void ThenTakeoverEvent(char const* expected) {
    do {
      ASSERT_TRUE(process->Line(line, Clock::now() + 10s)) << process->Transcript();
    } while (!line.starts_with("event ") || line.starts_with("event GEOMETRY ") ||
             line.starts_with("event CONNECTED "));
    EXPECT_TRUE(line.starts_with("event " + std::string(expected) + " ")) << line;
  }
  void WhenSmallerDesktop(Client& first) {
    ASSERT_TRUE(freerdp_connect(first.Instance().get())) << ConnectLogs();
    ASSERT_TRUE(Read("event GEOMETRY window=640x480 desktop=320x200"));
    ASSERT_TRUE(first.Until([&] { return Pattern(first, false); }));
    ASSERT_TRUE(freerdp_disconnect(first.Instance().get()));
    ASSERT_TRUE(Read("event FOCUS_LOST "));
  }
  void WhenWholeSampleReconnects(Client const& client, unsigned port) {
    ASSERT_TRUE(freerdp_disconnect(client.Instance().get())) << "disconnect";
    ASSERT_TRUE(Read("event OCCLUDED ")) << "OCCLUDED: " << process->Transcript();
    ASSERT_TRUE(Read("event FOCUS_LOST ")) << "FOCUS_LOST: " << process->Transcript();
    Client const second(port, true, 640, 480);
    ASSERT_TRUE(freerdp_connect(second.Instance().get())) << ConnectLogs() << "second session connects";
    Exposed();
    if (::testing::Test::HasFatalFailure()) return;
    Escape(second);
  }
  void GivenWholeSample() {
    process = std::make_unique<Process>(Arguments(certificates.Path(), false));
    ASSERT_TRUE(Read("port ")) << "port <n>: " << process->Transcript();
  }
};
class Sample : public SampleDesktopSteps {
protected:
  void ThenTouchEvent(Client& client, std::string_view event, std::string_view detail) {
    ASSERT_TRUE(ReadInput(client, event));
    EXPECT_TRUE(line.contains(detail)) << line;
  }
  void ThenIgnoredWarpMotion(Client& client, rdpInput* input, UINT16 x, UINT16 y, char const* delta) {
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, x, y));
    ASSERT_TRUE(Read("event MOUSE_MOTION "));
    EXPECT_TRUE(line.contains(delta)) << line;
    if (x == 630) ASSERT_TRUE(client.Until([&] { return Position().Count() > 0; }));
  }
  void GivenRelativeOrigin(rdpInput* input) {
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 200, 150));
    ASSERT_TRUE(Read("event MOUSE_MOTION "));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3d));
    ASSERT_TRUE(Read("event RELATIVE_MODE active=1"));
  }
  void WhenUnicodeControl(rdpInput* input, int code) {
    ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, code));
    ASSERT_TRUE(Read("event KEY_DOWN "));
    if (code == 27) EXPECT_TRUE(line.contains("scancode=41 key=27 down=1")) << line;
    EXPECT_TRUE(line.contains(" key=" + std::to_string(code) + " down=1")) << line;
  }
  void ThenStoppedScancodeText(std::size_t stopped) {
    EXPECT_EQ(process->Transcript().find("event TEXT_INPUT", stopped), std::string::npos);
    auto lines = std::string_view(process->Transcript()) | std::views::split('\n');
    EXPECT_EQ(
        std::ranges::count_if(lines, [](auto text) { return std::string_view(text).contains("event TEXT_INPUT"); }), 2);
    SDL_Log("gate SCANCODE_TEXT a=1 A=1 stopped_text=0");
  }
  void WhenReverseWheel(Client& client, auto* advanced) {
    ASSERT_EQ(advanced->AInputSendInputEvent(advanced, AINPUT_FLAGS_WHEEL, -120 * 65536, 120 * 65536), CHANNEL_RC_OK);
    ASSERT_TRUE(ReadInput(client, "event MOUSE_WHEEL "));
    EXPECT_TRUE(line.ends_with(" x=-1 y=1")) << line;
  }
  void WhenAspectRelative(Client& client) {
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x3d));
    ASSERT_TRUE(Read("event RELATIVE_MODE active=1"));
    auto* advanced = SampleGate::InputClient::Advanced().load();
    ASSERT_EQ(advanced->AInputSendInputEvent(advanced, AINPUT_FLAGS_MOVE | AINPUT_FLAGS_REL, -10, 48), CHANNEL_RC_OK);
    ASSERT_TRUE(ReadInput(client, "event MOUSE_MOTION "));
    EXPECT_TRUE(line.contains(" xrel=-10 yrel=35 ")) << line;
  }
  void WhenAdvancedMotion(Client& client, rdpInput* input) {
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3d));
    ASSERT_TRUE(Read("event RELATIVE_MODE active=1"));
    auto* advanced = SampleGate::InputClient::Advanced().load();
    ASSERT_EQ(advanced->AInputSendInputEvent(advanced, AINPUT_FLAGS_MOVE | AINPUT_FLAGS_REL, 17, -9), CHANNEL_RC_OK);
    ASSERT_TRUE(ReadInput(client, "event MOUSE_MOTION "));
    EXPECT_TRUE(line.contains(" xrel=17 yrel=-9 ")) << line;
  }
  void ThenWarpEchoIgnored(rdpInput* input) {
    auto initial = Position().Count();
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 320, 240));
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 330, 235));
    ASSERT_TRUE(Read("event MOUSE_MOTION "));
    EXPECT_TRUE(line.contains(" xrel=10 yrel=-5 ")) << line;
    EXPECT_EQ(Position().Count(), initial);
  }
  void WhenRelativeWarp(Client& client, rdpInput* input) {
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 630, 240));
    ASSERT_TRUE(Read("event MOUSE_MOTION "));
    ASSERT_TRUE(client.Until([&] { return Position().Count() > 0; }));
    EXPECT_EQ(Position().X(), 320u);
    EXPECT_EQ(Position().Y(), 240u);
    SDL_Log("gate POINTER_POSITION x=%u y=%u", Position().X(), Position().Y());
  }
  void WhenPreciseWheel(rdpInput* input, UINT16 flags, char const* expected) {
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, flags, 0, 0));
    ASSERT_TRUE(Read("event MOUSE_WHEEL "));
    EXPECT_TRUE(line.ends_with(expected)) << line;
  }
  void ThenStoppedUnicode(rdpInput* input) {
    auto stopped = process->Transcript().size();
    ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 0xe9));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x1e));
    ASSERT_TRUE(Read("event KEY_DOWN type=768 scancode=4 key=97 down=1"));
    EXPECT_TRUE(line.contains(" scancode=4 key=97 down=1")) << line;
    EXPECT_EQ(process->Transcript().find("event TEXT_INPUT", stopped), std::string::npos);
    SDL_Log("gate TEXT_STOPPED no_TEXT_INPUT=1 scancode_key=97 layout=0x040c");
  }
  void GivenFrenchKeyboard(Client const& client) {
    ASSERT_TRUE(freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_KeyboardLayout, 0x40c));
    ASSERT_TRUE(freerdp_connect(client.Instance().get()));
    ASSERT_TRUE(Read("event EXPOSED "));
    EXPECT_TRUE(line.contains(" keyboard_layout=1036 ")) << line;
    ASSERT_TRUE(Read("event FOCUS_GAINED "));
  }
};

}
