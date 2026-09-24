#include "support.test/client-steps.hpp"
#include "support.test/input-client.hpp"
#include "support.test/sample-launch.hpp"
#include "support.test/sample.hpp"

#include <SDL3/SDL.h>
#include <array>
#include <string>
#include <utility>

namespace SampleGate {

namespace {
auto WhenPressureContact(auto* touch, INT32& id) -> void {
  ASSERT_EQ(
      touch->TouchRawEvent(touch, 3, 160, 120, &id,
                           RDPINPUT_CONTACT_FLAG_DOWN | RDPINPUT_CONTACT_FLAG_INRANGE | RDPINPUT_CONTACT_FLAG_INCONTACT,
                           CONTACT_DATA_PRESSURE_PRESENT, UINT32(512)),
      CHANNEL_RC_OK);
}
auto TouchChannel(Client& client) -> auto* {
  bool const ready = client.Until([&] {
    return InputClient::Touch().load()
           && InputClient::Touch().load()->GetVersion(InputClient::Touch().load()) == RDPINPUT_PROTOCOL_V10;
  });
  EXPECT_TRUE(ready);
  return ready ? SampleGate::InputClient::Touch().load() : nullptr;
}

}

TEST_F(Sample, UnicodeTextAndStopped) {
  GivenProcess();
  if (::testing::Test::HasFatalFailure()) return;
  Client const client(AnnouncedPort(line), true, 640, 480);
  GivenFrenchKeyboard(client);
  if (::testing::Test::HasFatalFailure()) return;
  auto* input = client.Instance()->context->input;
  WhenUnicodeText(input);
  if (::testing::Test::HasFatalFailure()) return;
  WhenTextStops(input);
  if (::testing::Test::HasFatalFailure()) return;
  ThenStoppedUnicode(input);
  if (::testing::Test::HasFatalFailure()) return;
  Escape(client);
}

TEST_F(Sample, WheelBothAxesPrecise) {
  GivenInputSession();
  if (::testing::Test::HasFatalFailure()) return;
  auto& client = SessionClient();
  auto* input  = client.Instance()->context->input;
  for (auto [flags, expected] : std::array<std::pair<UINT16, char const*>, 4>{
           { { PTR_FLAGS_WHEEL | 30                                      , " x=0 y=0.25"  },
             { PTR_FLAGS_WHEEL | PTR_FLAGS_WHEEL_NEGATIVE | (0x200 - 60) , " x=0 y=-0.5"  },
             { PTR_FLAGS_HWHEEL | 120                                    , " x=1 y=0"     },
             { PTR_FLAGS_HWHEEL | PTR_FLAGS_WHEEL_NEGATIVE | (0x200 - 30), " x=-0.25 y=0" } } }) {
    WhenPreciseWheel(input, flags, expected);
    if (::testing::Test::HasFatalFailure()) return;
  }
  Escape(client);
}

TEST_F(Sample, RelativeWarpFallback) {
  GivenPositionSession();
  if (::testing::Test::HasFatalFailure()) return;
  auto& client = SessionClient();
  auto* input  = client.Instance()->context->input;
  WhenRelative(input);
  if (::testing::Test::HasFatalFailure()) return;
  WhenRelativeWarp(client, input);
  if (::testing::Test::HasFatalFailure()) return;
  ThenWarpEchoIgnored(input);
  if (::testing::Test::HasFatalFailure()) return;
  ThenAbsoluteMouse(input);
  if (::testing::Test::HasFatalFailure()) return;
  Escape(client);
}

TEST_F(Sample, AdvancedRelative) {
  GivenAdvancedSession();
  if (::testing::Test::HasFatalFailure()) return;
  auto& client = SessionClient();
  auto* input  = client.Instance()->context->input;
  WhenAdvancedMotion(client, input);
  if (::testing::Test::HasFatalFailure()) return;
  Escape(client);
}

TEST_F(Sample, TouchContacts) {
  GivenInputSession(true);
  if (::testing::Test::HasFatalFailure()) return;
  auto& client = SessionClient();
  auto* touch  = TouchChannel(client);
  ASSERT_NE(touch, nullptr);
  INT32 id = 0;
  ASSERT_EQ(touch->TouchBegin(touch, 7, 160, 120, &id), CHANNEL_RC_OK);
  ThenTouchEvent(client, "event FINGER_DOWN ", " x=0.250 y=0.250 ");
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_EQ(touch->TouchUpdate(touch, 7, 320, 240, &id), CHANNEL_RC_OK);
  ThenTouchEvent(client, "event FINGER_MOTION ", " x=0.500 y=0.500 ");
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_EQ(touch->TouchEnd(touch, 7, 320, 240, &id), CHANNEL_RC_OK);
  ThenTouchEvent(client, "event FINGER_UP ", "window_x=320 window_y=240");
  if (::testing::Test::HasFatalFailure()) return;
  Escape(client);
}

TEST_F(Sample, TouchPressureCancel) {
  GivenProcess();
  if (::testing::Test::HasFatalFailure()) return;
  Client            client(AnnouncedPort(line), true, 640, 480);
  InputClient const channels(client);
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  auto* touch = TouchChannel(client);
  ASSERT_NE(touch, nullptr);
  INT32 id = 0;
  WhenPressureContact(touch, id);
  if (::testing::Test::HasFatalFailure()) return;
  ThenTouchEvent(client, "event FINGER_DOWN ", "pressure=0.500");
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_EQ(touch->TouchCancel(touch, 3, 160, 120, &id), CHANNEL_RC_OK);
  ASSERT_TRUE(ReadInput(client, "event FINGER_CANCELED "));
  Escape(client);
}
TEST_F(Sample, AdvancedAspectRelative) {
  GivenAspect();
  if (::testing::Test::HasFatalFailure()) return;
  Client            client(AnnouncedPort(line), true, 640, 480);
  InputClient const channels(client);
  GivenFocus(client);
  if (::testing::Test::HasFatalFailure()) return;
  ThenAdvanced(client);
  if (::testing::Test::HasFatalFailure()) return;
  WhenAspectRelative(client);
  if (::testing::Test::HasFatalFailure()) return;
  Escape(client);
}

TEST_F(Sample, AdvancedWheelBothAxesPrecise) {
  GivenAdvancedSession();
  if (::testing::Test::HasFatalFailure()) return;
  auto& client   = SessionClient();
  auto* advanced = SampleGate::InputClient::Advanced().load();
  ASSERT_EQ(advanced->AInputSendInputEvent(advanced, AINPUT_FLAGS_WHEEL, 30 * 65536, -60 * 65536), CHANNEL_RC_OK);
  ASSERT_TRUE(ReadInput(client, "event MOUSE_WHEEL "));
  EXPECT_TRUE(line.ends_with(" x=0.25 y=-0.5")) << line;
  WhenReverseWheel(client, advanced);
  if (::testing::Test::HasFatalFailure()) return;
  Escape(client);
}
TEST_F(Sample, ScancodeTextAndStopped) {
  GivenInputSession();
  if (::testing::Test::HasFatalFailure()) return;
  auto& client = SessionClient();
  auto* input  = client.Instance()->context->input;
  WhenScancodeText(input);
  if (::testing::Test::HasFatalFailure()) return;
  WhenTextStops(input);
  if (::testing::Test::HasFatalFailure()) return;
  auto stopped = process->Transcript().size();
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x1e));
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x1e));
  ASSERT_TRUE(Read("event KEY_UP type=769 scancode=4 key=97 down=0"));
  Escape(client);
  if (::testing::Test::HasFatalFailure()) return;
  ThenStoppedScancodeText(stopped);
}

TEST_F(Sample, UnicodeKeysAndEscape) {
  GivenInputSession();
  if (::testing::Test::HasFatalFailure()) return;
  auto& client = SessionClient();
  auto* input  = client.Instance()->context->input;
  WhenUnicodeKeys(input);
  if (::testing::Test::HasFatalFailure()) return;
  auto controls = process->Transcript().size();
  for (auto code : { 8, 9, 13, 127, 27 }) {
    WhenUnicodeControl(input, code);
    if (::testing::Test::HasFatalFailure()) return;
  }
  EXPECT_TRUE(process->Exit());
  EXPECT_EQ(process->Transcript().find("event TEXT_INPUT", controls), std::string::npos);
  SDL_Log("gate UNICODE_KEYS a_key=97 a_text=1 controls=8,9,13,127,27 control_text=0 exit=ESCAPE");
}

TEST_F(Sample, RelativeIgnoredWarp) {
  GivenPositionSession();
  if (::testing::Test::HasFatalFailure()) return;
  auto& client = SessionClient();
  auto* input  = client.Instance()->context->input;
  GivenRelativeOrigin(input);
  if (::testing::Test::HasFatalFailure()) return;
  for (auto [x, y, delta] :
       std::array<std::tuple<UINT16, UINT16, char const*>, 4>{ { { 250, 200, " xrel=50 yrel=50 "   },
                                                                 { 300, 260, " xrel=50 yrel=60 "   },
                                                                 { 630, 260, " xrel=330 yrel=0 "   },
                                                                 { 580, 200, " xrel=-50 yrel=-60 " } } }) {
    ThenIgnoredWarpMotion(client, input, x, y, delta);
    if (::testing::Test::HasFatalFailure()) return;
  }
  SDL_Log("gate IGNORED_WARP deltas=50,50;50,60;330,0;-50,-60");
  Escape(client);
}

}
