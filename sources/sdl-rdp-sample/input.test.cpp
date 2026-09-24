#include <sdl-rdp/sample-gate.test/client-steps.hpp>
#include <sdl-rdp/sample-gate.test/input-client.hpp>
#include <sdl-rdp/sample-gate.test/sample-launch.hpp>
#include <sdl-rdp/sample-gate.test/sample.hpp>

#include <SDL3/SDL.h>
#include <sdl-rdp/headless-client.test/input-steps.hpp>
#include <array>
#include <string>
#include <utility>

namespace SampleGate {

namespace {
auto InputOf(Client const& client) -> rdpInput* {
  return client.Instance()->context->input;
}
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
  ASSERT_NO_FATAL_FAILURE(GivenProcess());
  auto const client = AnnouncedClient(640, 480);
  ASSERT_NO_FATAL_FAILURE(GivenFrenchKeyboard(client));
  auto* input = InputOf(client);
  ASSERT_NO_FATAL_FAILURE(WhenUnicodeText(input));
  ASSERT_NO_FATAL_FAILURE(WhenTextStops(client));
  ASSERT_NO_FATAL_FAILURE(ThenStoppedUnicode(input));
  Escape(client);
}

TEST_F(Sample, WheelBothAxesPrecise) {
  ASSERT_NO_FATAL_FAILURE(GivenInputSession());
  auto* input = InputOf(SessionClient());
  for (auto [flags, expected] : std::array<std::pair<UINT16, char const*>, 4>{
           { { PTR_FLAGS_WHEEL | 30                                      , " x=0 y=0.25"  },
             { PTR_FLAGS_WHEEL | PTR_FLAGS_WHEEL_NEGATIVE | (0x200 - 60) , " x=0 y=-0.5"  },
             { PTR_FLAGS_HWHEEL | 120                                    , " x=1 y=0"     },
             { PTR_FLAGS_HWHEEL | PTR_FLAGS_WHEEL_NEGATIVE | (0x200 - 30), " x=-0.25 y=0" } } }) {
    ASSERT_NO_FATAL_FAILURE(WhenPreciseWheel(input, flags, expected));
  }
  Escape(SessionClient());
}

TEST_F(Sample, RelativeWarpFallback) {
  ASSERT_NO_FATAL_FAILURE(GivenPositionSession());
  auto& client = SessionClient();
  auto* input  = InputOf(client);
  ASSERT_NO_FATAL_FAILURE(WhenRelative(client));
  ASSERT_NO_FATAL_FAILURE(WhenRelativeWarp(client, input));
  ASSERT_NO_FATAL_FAILURE(ThenWarpEchoIgnored(input));
  ASSERT_NO_FATAL_FAILURE(ThenAbsoluteMouse(input));
  Escape(client);
}

TEST_F(Sample, AdvancedRelative) {
  ASSERT_NO_FATAL_FAILURE(GivenAdvancedSession());
  auto& client = SessionClient();
  ASSERT_NO_FATAL_FAILURE(WhenAdvancedMotion(client));
  Escape(client);
}

TEST_F(Sample, TouchContacts) {
  ASSERT_NO_FATAL_FAILURE(GivenInputSession(true));
  auto& client = SessionClient();
  auto* touch  = TouchChannel(client);
  ASSERT_NE(touch, nullptr);
  INT32 id = 0;
  ASSERT_EQ(touch->TouchBegin(touch, 7, 160, 120, &id), CHANNEL_RC_OK);
  ASSERT_NO_FATAL_FAILURE(ThenTouchEvent(client, "event FINGER_DOWN ", " x=0.250 y=0.250 "));
  ASSERT_EQ(touch->TouchUpdate(touch, 7, 320, 240, &id), CHANNEL_RC_OK);
  ASSERT_NO_FATAL_FAILURE(ThenTouchEvent(client, "event FINGER_MOTION ", " x=0.500 y=0.500 "));
  ASSERT_EQ(touch->TouchEnd(touch, 7, 320, 240, &id), CHANNEL_RC_OK);
  ASSERT_NO_FATAL_FAILURE(ThenTouchEvent(client, "event FINGER_UP ", "window_x=320 window_y=240"));
  Escape(client);
}

TEST_F(Sample, TouchPressureCancel) {
  ASSERT_NO_FATAL_FAILURE(GivenProcess());
  auto              client   = AnnouncedClient(640, 480);
  InputClient const channels(client);
  ASSERT_TRUE(client.Connect());
  auto* touch = TouchChannel(client);
  ASSERT_NE(touch, nullptr);
  INT32 id = 0;
  ASSERT_NO_FATAL_FAILURE(WhenPressureContact(touch, id));
  ASSERT_NO_FATAL_FAILURE(ThenTouchEvent(client, "event FINGER_DOWN ", "pressure=0.500"));
  ASSERT_EQ(touch->TouchCancel(touch, 3, 160, 120, &id), CHANNEL_RC_OK);
  ASSERT_TRUE(ReadInput(client, "event FINGER_CANCELED "));
  Escape(client);
}
TEST_F(Sample, AdvancedAspectRelative) {
  ASSERT_NO_FATAL_FAILURE(GivenProcess({ }, AspectOptions()));
  auto              client   = AnnouncedClient(640, 480);
  InputClient const channels(client);
  ASSERT_NO_FATAL_FAILURE(GivenFocus(client));
  ASSERT_NO_FATAL_FAILURE(ThenAdvanced(client));
  ASSERT_NO_FATAL_FAILURE(WhenAspectRelative(client));
  Escape(client);
}

TEST_F(Sample, AdvancedWheelBothAxesPrecise) {
  ASSERT_NO_FATAL_FAILURE(GivenAdvancedSession());
  auto& client   = SessionClient();
  auto* advanced = SampleGate::InputClient::Advanced().load();
  ASSERT_EQ(advanced->AInputSendInputEvent(advanced, AINPUT_FLAGS_WHEEL, 30 * 65536, -60 * 65536), CHANNEL_RC_OK);
  ASSERT_TRUE(ReadInput(client, "event MOUSE_WHEEL "));
  EXPECT_TRUE(line.ends_with(" x=0.25 y=-0.5")) << line;
  ASSERT_NO_FATAL_FAILURE(WhenReverseWheel(client, advanced));
  Escape(client);
}
TEST_F(Sample, ScancodeTextAndStopped) {
  ASSERT_NO_FATAL_FAILURE(GivenInputSession());
  auto const& client = SessionClient();
  ASSERT_NO_FATAL_FAILURE(WhenScancodeText(client));
  ASSERT_NO_FATAL_FAILURE(WhenTextStops(client));
  auto stopped = process->Transcript().size();
  ASSERT_NO_FATAL_FAILURE(client.Tap(0x1e));
  ASSERT_TRUE(Read("event KEY_UP type=769 scancode=4 key=97 down=0"));
  ASSERT_NO_FATAL_FAILURE(Escape(client));
  ThenStoppedScancodeText(stopped);
}

TEST_F(Sample, UnicodeKeysAndEscape) {
  ASSERT_NO_FATAL_FAILURE(GivenInputSession());
  auto* input = InputOf(SessionClient());
  ASSERT_NO_FATAL_FAILURE(WhenUnicodeKeys(input));
  auto controls = process->Transcript().size();
  for (auto code : { 8, 9, 13, 127, 27 }) {
    ASSERT_NO_FATAL_FAILURE(WhenUnicodeControl(input, code));
  }
  EXPECT_TRUE(process->Exit());
  EXPECT_EQ(process->Transcript().find("event TEXT_INPUT", controls), std::string::npos);
  SDL_Log("gate UNICODE_KEYS a_key=97 a_text=1 controls=8,9,13,127,27 control_text=0 exit=ESCAPE");
}

TEST_F(Sample, RelativeIgnoredWarp) {
  ASSERT_NO_FATAL_FAILURE(GivenPositionSession());
  auto& client = SessionClient();
  auto* input  = InputOf(client);
  ASSERT_NO_FATAL_FAILURE(GivenRelativeOrigin(client));
  for (auto [x, y, delta] :
       std::array<std::tuple<UINT16, UINT16, char const*>, 4>{ { { 250, 200, " xrel=50 yrel=50 "   },
                                                                 { 300, 260, " xrel=50 yrel=60 "   },
                                                                 { 630, 260, " xrel=330 yrel=0 "   },
                                                                 { 580, 200, " xrel=-50 yrel=-60 " } } }) {
    ASSERT_NO_FATAL_FAILURE(ThenIgnoredWarpMotion(client, input, x, y, delta));
  }
  SDL_Log("gate IGNORED_WARP deltas=50,50;50,60;330,0;-50,-60");
  Escape(client);
}

}
