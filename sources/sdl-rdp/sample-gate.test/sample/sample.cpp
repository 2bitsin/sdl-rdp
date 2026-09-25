#include <sdl-rdp/sample-gate.test/sample/sample.hpp>

#include <sdl-rdp/sample-gate.test/client/input.hpp>

#include <SDL3/SDL.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string>

namespace SampleGate {
auto Sample::ThenTouchEvent(Client& client, std::string_view event, std::string_view detail) -> void {
  ASSERT_TRUE(ReadInput(client, event));
  EXPECT_TRUE(line.contains(detail)) << line;
}
auto Sample::ThenIgnoredWarpMotion(Client& client, rdpInput* input, std::uint16_t x, std::uint16_t y, char const* delta)
    -> void {
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, x, y));
  ASSERT_TRUE(Read("event MOUSE_MOTION "));
  EXPECT_TRUE(line.contains(delta)) << line;
  if (x == 630) ASSERT_TRUE(client.Until([&] { return Position().Count() > 0; }));
}
auto Sample::GivenRelativeOrigin(Client const& client) -> void {
  ASSERT_TRUE(freerdp_input_send_mouse_event(client.Instance()->context->input, PTR_FLAGS_MOVE, 200, 150));
  ASSERT_TRUE(Read("event MOUSE_MOTION "));
  ASSERT_NO_FATAL_FAILURE(WhenRelative(client));
}
auto Sample::WhenUnicodeControl(rdpInput* input, int code) -> void {
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, code));
  ASSERT_TRUE(Read("event KEY_DOWN "));
  if (code == 27) EXPECT_TRUE(line.contains("scancode=41 key=27 down=1")) << line;
  EXPECT_TRUE(line.contains(" key=" + std::to_string(code) + " down=1")) << line;
}
auto Sample::ThenStoppedScancodeText(std::size_t stopped) -> void {
  EXPECT_EQ(process->Transcript().find("event TEXT_INPUT", stopped), std::string::npos);
  auto lines = std::string_view(process->Transcript()) | std::views::split('\n');
  EXPECT_EQ(std::ranges::count_if(lines, [](auto text) { return std::string_view(text).contains("event TEXT_INPUT"); }),
            2);
  SDL_Log("gate SCANCODE_TEXT a=1 A=1 stopped_text=0");
}
auto Sample::WhenAspectRelative(Client& client) -> void {
  WhenRelativeAdvanced(client, -10, 48, " xrel=-10 yrel=35 ");
}
auto Sample::WhenAdvancedMotion(Client& client) -> void {
  WhenRelativeAdvanced(client, 17, -9, " xrel=17 yrel=-9 ");
}
auto Sample::WhenRelativeAdvanced(Client& client, std::int32_t x, std::int32_t y, std::string_view expected) -> void {
  ASSERT_NO_FATAL_FAILURE(WhenRelative(client));
  auto* advanced = SampleGate::InputClient::Advanced().load();
  ASSERT_EQ(advanced->AInputSendInputEvent(advanced, AINPUT_FLAGS_MOVE | AINPUT_FLAGS_REL, x, y), CHANNEL_RC_OK);
  ASSERT_TRUE(ReadInput(client, "event MOUSE_MOTION "));
  EXPECT_TRUE(line.contains(expected)) << line;
}
auto Sample::ThenWarpEchoIgnored(rdpInput* input) -> void {
  auto initial = Position().Count();
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 320, 240));
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 330, 235));
  ASSERT_TRUE(Read("event MOUSE_MOTION "));
  EXPECT_TRUE(line.contains(" xrel=10 yrel=-5 ")) << line;
  EXPECT_EQ(Position().Count(), initial);
}
auto Sample::WhenRelativeWarp(Client& client, rdpInput* input) -> void {
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 630, 240));
  ASSERT_TRUE(Read("event MOUSE_MOTION "));
  ASSERT_TRUE(client.Until([&] { return Position().Count() > 0; }));
  EXPECT_EQ(Position().X(), 320u);
  EXPECT_EQ(Position().Y(), 240u);
  SDL_Log("gate POINTER_POSITION x=%u y=%u", Position().X(), Position().Y());
}
auto Sample::WhenPreciseWheel(rdpInput* input, std::uint16_t flags, char const* expected) -> void {
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, flags, 0, 0));
  ASSERT_TRUE(Read("event MOUSE_WHEEL "));
  EXPECT_TRUE(line.ends_with(expected)) << line;
}
auto Sample::ThenStoppedUnicode(rdpInput* input) -> void {
  auto stopped = process->Transcript().size();
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 0xe9));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x1e));
  ASSERT_TRUE(Read("event KEY_DOWN type=768 scancode=4 key=97 down=1"));
  EXPECT_TRUE(line.contains(" scancode=4 key=97 down=1")) << line;
  EXPECT_EQ(process->Transcript().find("event TEXT_INPUT", stopped), std::string::npos);
  SDL_Log("gate TEXT_STOPPED no_TEXT_INPUT=1 scancode_key=97 layout=0x040c");
}
auto Sample::GivenFrenchKeyboard(Client const& client) -> void {
  ASSERT_TRUE(freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_KeyboardLayout, 0x40c));
  ASSERT_TRUE(client.Connect());
  ASSERT_TRUE(Read("event EXPOSED "));
  EXPECT_TRUE(line.contains(" keyboard_layout=1036 ")) << line;
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
}
}
