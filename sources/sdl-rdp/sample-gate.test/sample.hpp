#pragma once
#include <sdl-rdp/sample-gate.test/sample-desktop-steps.hpp>

namespace SampleGate {
class Sample : public SampleDesktopSteps {
protected:
  auto ThenTouchEvent(Client& client, std::string_view event, std::string_view detail)               -> void;
  auto ThenIgnoredWarpMotion(Client& client, rdpInput* input, UINT16 x, UINT16 y, char const* delta) -> void;
  auto GivenRelativeOrigin(rdpInput* input)                                                          -> void;
  auto WhenUnicodeControl(rdpInput* input, int code)                                                 -> void;
  auto ThenStoppedScancodeText(std::size_t stopped)                                                  -> void;
  auto WhenReverseWheel(Client& client, auto* advanced)                                              -> void {
    ASSERT_EQ(advanced->AInputSendInputEvent(advanced, AINPUT_FLAGS_WHEEL, -120 * 65536, 120 * 65536), CHANNEL_RC_OK);
    ASSERT_TRUE(ReadInput(client, "event MOUSE_WHEEL "));
    EXPECT_TRUE(line.ends_with(" x=-1 y=1")) << line;
  }
  auto WhenAspectRelative(Client& client)                                    -> void;
  auto WhenAdvancedMotion(Client& client, rdpInput* input)                   -> void;
  auto ThenWarpEchoIgnored(rdpInput* input)                                  -> void;
  auto WhenRelativeWarp(Client& client, rdpInput* input)                     -> void;
  auto WhenPreciseWheel(rdpInput* input, UINT16 flags, char const* expected) -> void;
  auto ThenStoppedUnicode(rdpInput* input)                                   -> void;
  auto GivenFrenchKeyboard(Client const& client)                             -> void;
};
}
