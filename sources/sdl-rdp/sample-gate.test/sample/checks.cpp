#include <sdl-rdp/sample-gate.test/sample/checks.hpp>

#include <SDL3/SDL.h>
#include <freerdp/channels/rdpdr.h>
#include <openssl/evp.h>
#include <oxbox/utilities/hex.hpp>
#include <sdl-rdp/headless-client.test/drive/observer.hpp>
#include <sdl-rdp/headless-client.test/drive/share-drive.hpp>
#include <sdl-rdp/headless-client.test/input/steps.hpp>
#include <sdl-rdp/headless-client.test/utilities/io.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace SampleGate {
namespace {
auto ThenWrittenBytes(std::string const& output) -> void {
  for (std::size_t i = 0; i < output.size(); ++i) {
    auto expected = i >= 1024uz * 1024 && i < static_cast<std::ptrdiff_t>(2 * 1024) * 1024
                        ? 0
                        : (i % (1024uz * 1024)) % 251;
    ASSERT_EQ(static_cast<std::uint8_t>(output[i]), expected) << i;
  }
}
}

auto SampleChecks::WhenSurrogateText(rdpInput* input) -> void {
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 0xd83d));
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 0xde00));
  ASSERT_TRUE(Read("event TEXT_INPUT text=😀"));
}
auto SampleChecks::WhenUnicodeText(rdpInput* input) -> void {
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 0xe9));
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_RELEASE, 0xe9));
  ASSERT_TRUE(Read("event TEXT_INPUT text=é"));
  WhenSurrogateText(input);
}
auto SampleChecks::ThenAbsoluteMouse(rdpInput* input) -> void {
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3d));
  ASSERT_TRUE(Read("event RELATIVE_MODE active=0"));
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 100, 120));
  ASSERT_TRUE(Read("event MOUSE_MOTION "));
  EXPECT_TRUE(line.contains(" x=100 y=120 ")) << line;
}
auto SampleChecks::WhenShiftedText(Client const& client) -> void {
  auto* input = client.Instance()->context->input;
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x2a));
  ASSERT_NO_FATAL_FAILURE(Headless::Tap(client, 0x1e));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x2a));
  ASSERT_TRUE(Read("event TEXT_INPUT text=A"));
}
auto SampleChecks::WhenScancodeText(Client const& client) -> void {
  ASSERT_NO_FATAL_FAILURE(Headless::Tap(client, 0x1e));
  ASSERT_TRUE(Read("event TEXT_INPUT text=a"));
  WhenShiftedText(client);
}
auto SampleChecks::WhenNonAsciiKey(rdpInput* input) -> void {
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 0xe4));
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_RELEASE, 0xe4));
  ASSERT_TRUE(Read("event KEY_DOWN type=768 scancode=400 key=0 down=1"));
  ASSERT_TRUE(Read("event KEY_UP type=769 scancode=400 key=0 down=0"));
  ASSERT_TRUE(Read("event TEXT_INPUT text=ä"));
}
auto SampleChecks::ThenUnicodeKeyEvents() -> void {
  ASSERT_TRUE(Read("event KEY_DOWN "));
  EXPECT_TRUE(line.contains("scancode=4 key=97 down=1")) << line;
  ASSERT_TRUE(Read("event KEY_UP type=769 scancode=4 key=97 down=0"));
  ASSERT_TRUE(Read("event TEXT_INPUT text=a"));
}
auto SampleChecks::WhenUnicodeKeys(rdpInput* input) -> void {
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 'a'));
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_RELEASE, 'a'));
  ASSERT_NO_FATAL_FAILURE(ThenUnicodeKeyEvents());
  WhenNonAsciiKey(input);
}
auto SampleChecks::ThenDriveOutput(fs::path const& share, std::string const& original) -> void {
  std::array<std::uint8_t, EVP_MAX_MD_SIZE> digest { };
  std::uint32_t                             length = 0;
  ASSERT_EQ(EVP_Digest(original.data(), original.size(), digest.data(), &length, EVP_sha256(), nullptr), 1);
  auto hex = oxbox::utilities::ToHex(std::as_bytes(std::span(digest).first(length)));
  ASSERT_TRUE(Read("cat bytes=21 sha256=" + hex)) << process->Transcript();
  ASSERT_TRUE(Read("write done")) << process->Transcript();
  auto output = Headless::ReadText((share / "output").c_str());
  ASSERT_EQ(output.size(), 3 * 1024uz * 1024u);
  ThenWrittenBytes(output);
}
auto SampleChecks::WhenSettingsEnvironmentConflicts() -> void {
  ASSERT_TRUE(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), SDL_HINT_RDP_PORT, "2", true));
  ASSERT_TRUE(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), SDL_HINT_RDP_SETTINGS, "/missing/settings.yaml", true));
  ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
}
auto SampleChecks::GivenSettingsHints(fs::path const& file) -> void {
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp"));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_SETTINGS, file.c_str()));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_CERT_DIR, certificates.Path().c_str()));
  ASSERT_TRUE(SDL_SetHintWithPriority(SDL_HINT_RDP_PORT, "0", SDL_HINT_OVERRIDE));
  WhenSettingsEnvironmentConflicts();
}
auto SampleChecks::ThenReloadedAspect() -> void {
  auto* window = SDL_CreateWindow("reloaded settings", 640, 480, 0);
  ASSERT_NE(window, nullptr);
  EXPECT_STREQ(SDL_GetStringProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_RDP_ASPECT_STRING, ""), "2:1");
  SDL_DestroyWindow(window);
  SDL_Quit();
}
auto SampleChecks::ThenReloadedSettings(fs::path const& file) -> void {
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_SETTINGS, file.c_str()));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp"));
  ASSERT_TRUE(SDL_SetHintWithPriority(SDL_HINT_RDP_PORT, "0", SDL_HINT_OVERRIDE));
  ASSERT_TRUE(SDL_SetHint(SDL_HINT_RDP_CERT_DIR, certificates.Path().c_str()));
  ASSERT_TRUE(SDL_Init(SDL_INIT_VIDEO)) << SDL_GetError();
  ThenReloadedAspect();
}
auto SampleChecks::DisconnectReading(std::uint32_t port, fs::path const& share) -> void {
  {
    Client client(port, true, 640, 480);
    Headless::ShareDrive(client, share.c_str());
    ASSERT_NO_FATAL_FAILURE(Connect(client));
    Headless::DriveObserver observer(client);
    ASSERT_TRUE(client.Until([&] {
      return std::ranges::any_of(observer.Observed().io, [](sdl_rdp::drive::DrivePacket packet) {
        packet.Skip(12);
        return packet.Read<std::uint32_t>() == IRP_MJ_READ;
      });
    }));
    ASSERT_TRUE(client.Disconnect());
  }
}
auto SampleChecks::ThenClipboardCleared(Client& client, Headless::ClipboardClient& clipboard) -> void {
  ASSERT_TRUE(clipboard.Offer({ }, false));
  ASSERT_TRUE(client.Until([&] { return clipboard.Observed().accepted.load() == 4; }));
  ASSERT_TRUE(Read("event CLIPBOARD text="));
  SDL_Log("trace CLIPBOARD client formats=8 text-cleared=1");
}
}
