#pragma once
#include "sample-session.hpp"

#include <format>
#include <openssl/evp.h>
#include <oxbox/utilities/hex.hpp>
#include <sdl-rdp-backend.so/_detail/headless-drive.hpp>
#include <sdl-rdp-backend.so/_detail/test-io.hpp>

namespace SampleGate {
inline void ThenWrittenBytes(std::string const& output) {
  for (size_t i = 0; i < output.size(); ++i) {
    auto expected =
        i >= 1024uz * 1024 && i < static_cast<std::ptrdiff_t>(2 * 1024) * 1024 ? 0 : (i % (1024uz * 1024)) % 251;
    ASSERT_EQ(static_cast<unsigned char>(output[i]), expected) << i;
  }
}
class SampleChecks : public SampleProcess {
protected:
  void WhenSurrogateText(rdpInput* input) {
    ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 0xd83d));
    ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 0xde00));
    ASSERT_TRUE(Read("event TEXT_INPUT text=😀"));
  }
  void WhenUnicodeText(rdpInput* input) {
    ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 0xe9));
    ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_RELEASE, 0xe9));
    ASSERT_TRUE(Read("event TEXT_INPUT text=é"));
    WhenSurrogateText(input);
  }
  void ThenAbsoluteMouse(rdpInput* input) {
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3d));
    ASSERT_TRUE(Read("event RELATIVE_MODE active=0"));
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 100, 120));
    ASSERT_TRUE(Read("event MOUSE_MOTION "));
    EXPECT_TRUE(line.contains(" x=100 y=120 ")) << line;
  }
  void WhenShiftedText(rdpInput* input) {
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x2a));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x1e));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x1e));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x2a));
    ASSERT_TRUE(Read("event TEXT_INPUT text=A"));
  }
  void WhenScancodeText(rdpInput* input) {
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x1e));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x1e));
    ASSERT_TRUE(Read("event TEXT_INPUT text=a"));
    WhenShiftedText(input);
  }
  void WhenNonAsciiKey(rdpInput* input) {
    ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 0xe4));
    ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_RELEASE, 0xe4));
    ASSERT_TRUE(Read("event KEY_DOWN type=768 scancode=400 key=0 down=1"));
    ASSERT_TRUE(Read("event KEY_UP type=769 scancode=400 key=0 down=0"));
    ASSERT_TRUE(Read("event TEXT_INPUT text=ä"));
  }
  void ThenUnicodeKeyEvents() {
    ASSERT_TRUE(Read("event KEY_DOWN "));
    EXPECT_TRUE(line.contains("scancode=4 key=97 down=1")) << line;
    ASSERT_TRUE(Read("event KEY_UP type=769 scancode=4 key=97 down=0"));
    ASSERT_TRUE(Read("event TEXT_INPUT text=a"));
  }
  void WhenUnicodeKeys(rdpInput* input) {
    ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 'a'));
    ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_RELEASE, 'a'));
    ThenUnicodeKeyEvents();
    if (::testing::Test::HasFatalFailure()) return;
    WhenNonAsciiKey(input);
  }
  void ThenDriveOutput(fs::path const& share, std::string const& original) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{ };
    unsigned                                   length = 0;
    ASSERT_EQ(EVP_Digest(original.data(), original.size(), digest.data(), &length, EVP_sha256(), nullptr), 1);
    auto hex = oxbox::utilities::ToHex(std::as_bytes(std::span(digest).first(length)));
    ASSERT_TRUE(Read("cat bytes=21 sha256=" + hex)) << process->Transcript();
    ASSERT_TRUE(Read("write done")) << process->Transcript();
    auto output = Headless::ReadText((share / "output").c_str());
    ASSERT_EQ(output.size(), 3 * 1024uz * 1024u);
    ThenWrittenBytes(output);
  }
  static void WhenIniEnvironmentConflicts() {
    ASSERT_TRUE(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), SDL_HINT_RDP_PORT, "2", true));
    ASSERT_TRUE(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), SDL_HINT_RDP_INI, "/missing/ini", true));
    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
  }
  void GivenIniHints(fs::path const& file) {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp"));
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_INI, file.c_str()));
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_CERT_DIR, certificates.Path().c_str()));
    ASSERT_TRUE(SDL_SetHintWithPriority(SDL_HINT_RDP_PORT, "0", SDL_HINT_OVERRIDE));
    WhenIniEnvironmentConflicts();
  }
  static void ThenCachedAspect() {
    auto* window = SDL_CreateWindow("cached ini", 640, 480, 0);
    ASSERT_NE(window, nullptr);
    EXPECT_STREQ(SDL_GetStringProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_RDP_ASPECT_STRING, ""), "4:3");
    SDL_DestroyWindow(window);
    SDL_Quit();
  }
  void ThenCachedIni() {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp"));
    ASSERT_TRUE(SDL_SetHintWithPriority(SDL_HINT_RDP_PORT, "0", SDL_HINT_OVERRIDE));
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_CERT_DIR, certificates.Path().c_str()));
    ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
    ThenCachedAspect();
  }
  void DisconnectReading(unsigned port, fs::path const& share) {
    {
      Client client(port, true, 640, 480);
      Headless::ShareDrive(client, share.c_str());
      ASSERT_TRUE(freerdp_connect(client.Instance().get())) << ConnectLogs();
      Headless::DriveObserver observer(client);
      ASSERT_TRUE(client.Until([&] {
        return std::ranges::any_of(observer.Observed().io, [](auto packet) {
          packet.Skip(12);
          return packet.Get(4) == IRP_MJ_READ;
        });
      }));
      ASSERT_TRUE(freerdp_disconnect(client.Instance().get()));
    }
  }
  void ThenClipboardCleared(Client& client, Headless::ClipboardClient& clipboard) {
    ASSERT_EQ(clipboard.Offer({ }, false), CHANNEL_RC_OK);
    ASSERT_TRUE(client.Until([&] { return clipboard.Observed().accepted.load() == 4; }));
    ASSERT_TRUE(Read("event CLIPBOARD text="));
    SDL_Log("trace CLIPBOARD client formats=8 text-cleared=1");
  }
};
}
