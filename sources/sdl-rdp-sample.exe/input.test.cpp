#include "_detail/sample-fixture.hpp"
#include "_detail/input-client.hpp"

namespace SampleGate {

TEST_F(Sample, UnicodeTextAndStopped) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_KeyboardLayout, 0x40c));
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(Read("event EXPOSED "));
  EXPECT_TRUE(line.contains(" keyboard_layout=1036 ")) << line;
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
  auto input = client.instance->context->input;
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 0xe9));
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_RELEASE, 0xe9));
  ASSERT_TRUE(Read("event TEXT_INPUT text=é"));
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 0xd83d));
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 0xde00));
  ASSERT_TRUE(Read("event TEXT_INPUT text=😀"));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3c));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3c));
  ASSERT_TRUE(Read("event TEXT_MODE active=0"));
  auto stopped = process->transcript.size();
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 0xe9));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x1e));
  ASSERT_TRUE(Read("event KEY_DOWN type=768 scancode=4 key=97 down=1"));
  EXPECT_TRUE(line.contains(" scancode=4 key=97 down=1")) << line;
  EXPECT_EQ(process->transcript.find("event TEXT_INPUT", stopped), std::string::npos);
  SDL_Log("gate TEXT_STOPPED no_TEXT_INPUT=1 scancode_key=97 layout=0x040c");
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, WheelBothAxesPrecise) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
  auto input = client.instance->context->input;
  for (auto [flags, expected] : std::array<std::pair<UINT16, const char*>, 4>{{
      {PTR_FLAGS_WHEEL | 30, " x=0 y=0.25"},
      {PTR_FLAGS_WHEEL | PTR_FLAGS_WHEEL_NEGATIVE | (0x200 - 60), " x=0 y=-0.5"},
      {PTR_FLAGS_HWHEEL | 120, " x=1 y=0"},
      {PTR_FLAGS_HWHEEL | PTR_FLAGS_WHEEL_NEGATIVE | (0x200 - 30), " x=-0.25 y=0"}}}) {
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, flags, 0, 0));
    ASSERT_TRUE(Read("event MOUSE_WHEEL "));
    EXPECT_TRUE(line.ends_with(expected)) << line;
  }
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, RelativeWarpFallback) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  PositionObserver position(client);
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
  auto input = client.instance->context->input;
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3d));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3d));
  ASSERT_TRUE(Read("event RELATIVE_MODE active=1"));
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 630, 240));
  ASSERT_TRUE(Read("event MOUSE_MOTION "));
  ASSERT_TRUE(client.Until([&] { return position.count > 0; }));
  EXPECT_EQ(position.x, 320u); EXPECT_EQ(position.y, 240u);
  SDL_Log("gate POINTER_POSITION x=%u y=%u", position.x, position.y);
  auto initial = position.count;
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 320, 240));
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 330, 235));
  ASSERT_TRUE(Read("event MOUSE_MOTION "));
  EXPECT_TRUE(line.contains(" xrel=10 yrel=-5 ")) << line;
  EXPECT_EQ(position.count, initial);
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3d));
  ASSERT_TRUE(Read("event RELATIVE_MODE active=0"));
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 100, 120));
  ASSERT_TRUE(Read("event MOUSE_MOTION "));
  EXPECT_TRUE(line.contains(" x=100 y=120 ")) << line;
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, AdvancedRelative) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  InputClient channels(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
  ASSERT_TRUE(client.Until([&] { return channels.advanced.load() && channels.touch.load() && channels.touch.load()->GetVersion(channels.touch.load()) == RDPINPUT_PROTOCOL_V10; }));
  auto input = client.instance->context->input;
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3d));
  ASSERT_TRUE(Read("event RELATIVE_MODE active=1"));
  auto advanced = channels.advanced.load();
  ASSERT_EQ(advanced->AInputSendInputEvent(advanced, AINPUT_FLAGS_MOVE | AINPUT_FLAGS_REL, 17, -9), CHANNEL_RC_OK);
  ASSERT_TRUE(ReadInput(client, "event MOUSE_MOTION "));
  EXPECT_TRUE(line.contains(" xrel=17 yrel=-9 ")) << line;
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, TouchContacts) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  InputClient channels(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
  ASSERT_TRUE(client.Until([&] { return channels.touch.load() && channels.touch.load()->GetVersion(channels.touch.load()) == RDPINPUT_PROTOCOL_V10; }));
  auto touch = channels.touch.load();
  INT32 id;
  ASSERT_EQ(touch->TouchBegin(touch, 7, 160, 120, &id), CHANNEL_RC_OK);
  ASSERT_TRUE(ReadInput(client, "event FINGER_DOWN "));
  EXPECT_TRUE(line.contains(" x=0.250 y=0.250 ")) << line;
  ASSERT_EQ(touch->TouchUpdate(touch, 7, 320, 240, &id), CHANNEL_RC_OK);
  ASSERT_TRUE(ReadInput(client, "event FINGER_MOTION "));
  EXPECT_TRUE(line.contains(" x=0.500 y=0.500 ")) << line;
  ASSERT_EQ(touch->TouchEnd(touch, 7, 320, 240, &id), CHANNEL_RC_OK);
  ASSERT_TRUE(ReadInput(client, "event FINGER_UP "));
  EXPECT_TRUE(line.contains("window_x=320 window_y=240")) << line;
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, TouchPressureCancel) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  InputClient channels(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return channels.touch.load() && channels.touch.load()->GetVersion(channels.touch.load()) == RDPINPUT_PROTOCOL_V10; }));
  auto touch = channels.touch.load();
  INT32 id;
  ASSERT_EQ(touch->TouchRawEvent(touch, 3, 160, 120, &id,
    RDPINPUT_CONTACT_FLAG_DOWN | RDPINPUT_CONTACT_FLAG_INRANGE | RDPINPUT_CONTACT_FLAG_INCONTACT,
    CONTACT_DATA_PRESSURE_PRESENT, UINT32(512)), CHANNEL_RC_OK);
  ASSERT_TRUE(ReadInput(client, "event FINGER_DOWN "));
  EXPECT_TRUE(line.contains("pressure=0.500")) << line;
  ASSERT_EQ(touch->TouchCancel(touch, 3, 160, 120, &id), CHANNEL_RC_OK);
  ASSERT_TRUE(ReadInput(client, "event FINGER_CANCELED "));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}
TEST_F(Sample, AdvancedAspectRelative) {
  auto arguments = Arguments(certificates.Path(), false);
  arguments.insert(arguments.end(), {"--size", "640x350", "--aspect", "4:3"});
  process = std::make_unique<Process>(arguments);
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  InputClient channels(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
  ASSERT_TRUE(client.Until([&] { return channels.advanced.load() && channels.touch.load() && channels.touch.load()->GetVersion(channels.touch.load()) == RDPINPUT_PROTOCOL_V10; }));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x3d));
  ASSERT_TRUE(Read("event RELATIVE_MODE active=1"));
  auto advanced = channels.advanced.load();
  ASSERT_EQ(advanced->AInputSendInputEvent(advanced, AINPUT_FLAGS_MOVE | AINPUT_FLAGS_REL, -10, 48), CHANNEL_RC_OK);
  ASSERT_TRUE(ReadInput(client, "event MOUSE_MOTION "));
  EXPECT_TRUE(line.contains(" xrel=-10 yrel=35 ")) << line;
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

TEST_F(Sample, AdvancedWheelBothAxesPrecise) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  InputClient channels(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
  ASSERT_TRUE(client.Until([&] { return channels.advanced.load() && channels.touch.load() && channels.touch.load()->GetVersion(channels.touch.load()) == RDPINPUT_PROTOCOL_V10; }));
  auto advanced = channels.advanced.load();
  ASSERT_EQ(advanced->AInputSendInputEvent(advanced, AINPUT_FLAGS_WHEEL, 30 * 65536, -60 * 65536), CHANNEL_RC_OK);
  ASSERT_TRUE(ReadInput(client, "event MOUSE_WHEEL "));
  EXPECT_TRUE(line.ends_with(" x=0.25 y=-0.5")) << line;
  ASSERT_EQ(advanced->AInputSendInputEvent(advanced, AINPUT_FLAGS_WHEEL, -120 * 65536, 120 * 65536), CHANNEL_RC_OK);
  ASSERT_TRUE(ReadInput(client, "event MOUSE_WHEEL "));
  EXPECT_TRUE(line.ends_with(" x=-1 y=1")) << line;
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}
TEST_F(Sample, ScancodeTextAndStopped) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
  auto input = client.instance->context->input;
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x1e));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x1e));
  ASSERT_TRUE(Read("event TEXT_INPUT text=a"));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x2a));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x1e));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x1e));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x2a));
  ASSERT_TRUE(Read("event TEXT_INPUT text=A"));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3c));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x3c));
  ASSERT_TRUE(Read("event TEXT_MODE active=0"));
  auto stopped = process->transcript.size();
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x1e));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x1e));
  ASSERT_TRUE(Read("event KEY_UP type=769 scancode=4 key=97 down=0"));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
  EXPECT_EQ(process->transcript.find("event TEXT_INPUT", stopped), std::string::npos);
  auto lines = std::string_view(process->transcript) | std::views::split('\n');
  EXPECT_EQ(std::ranges::count_if(lines, [](auto text) {
    return std::string_view(text).contains("event TEXT_INPUT");
  }), 2);
  SDL_Log("gate SCANCODE_TEXT a=1 A=1 stopped_text=0");
}

TEST_F(Sample, UnicodeKeysAndEscape) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
  auto input = client.instance->context->input;
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 'a'));
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_RELEASE, 'a'));
  ASSERT_TRUE(Read("event KEY_DOWN "));
  EXPECT_TRUE(line.contains("scancode=4 key=97 down=1")) << line;
  ASSERT_TRUE(Read("event KEY_UP type=769 scancode=4 key=97 down=0"));
  ASSERT_TRUE(Read("event TEXT_INPUT text=a"));
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, 0xe4));
  ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_RELEASE, 0xe4));
  ASSERT_TRUE(Read("event KEY_DOWN type=768 scancode=400 key=0 down=1"));
  ASSERT_TRUE(Read("event KEY_UP type=769 scancode=400 key=0 down=0"));
  ASSERT_TRUE(Read("event TEXT_INPUT text=ä"));
  auto controls = process->transcript.size();
  for (auto code : {8, 9, 13, 127, 27}) {
    ASSERT_TRUE(freerdp_input_send_unicode_keyboard_event(input, KBD_FLAGS_DOWN, code));
    ASSERT_TRUE(Read("event KEY_DOWN "));
    if (code == 27) EXPECT_TRUE(line.contains("scancode=41 key=27 down=1")) << line;
    EXPECT_TRUE(line.contains(" key=" + std::to_string(code) + " down=1")) << line;
  }
  EXPECT_TRUE(process->Exit());
  EXPECT_EQ(process->transcript.find("event TEXT_INPUT", controls), std::string::npos);
  SDL_Log("gate UNICODE_KEYS a_key=97 a_text=1 controls=8,9,13,127,27 control_text=0 exit=ESCAPE");
}

TEST_F(Sample, RelativeIgnoredWarp) {
  process = std::make_unique<Process>(Arguments(certificates.Path(), false));
  ASSERT_TRUE(Read("port "));
  Client client(Number(std::string_view(line).substr(5)), true, 640, 480);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  PositionObserver position(client);
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
  auto input = client.instance->context->input;
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 200, 150));
  ASSERT_TRUE(Read("event MOUSE_MOTION "));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x3d));
  ASSERT_TRUE(Read("event RELATIVE_MODE active=1"));
  for (auto [x, y, delta] : std::array<std::tuple<UINT16, UINT16, const char*>, 4>{{
      {250, 200, " xrel=50 yrel=50 "}, {300, 260, " xrel=50 yrel=60 "},
      {630, 260, " xrel=330 yrel=0 "}, {580, 200, " xrel=-50 yrel=-60 "}}}) {
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, x, y));
    ASSERT_TRUE(Read("event MOUSE_MOTION "));
    EXPECT_TRUE(line.contains(delta)) << line;
    if (x == 630) ASSERT_TRUE(client.Until([&] { return position.count > 0; }));
  }
  SDL_Log("gate IGNORED_WARP deltas=50,50;50,60;330,0;-50,-60");
  ASSERT_NO_FATAL_FAILURE(Escape(client));
}

}
