#pragma once
#include <sdl-rdp/sample-gate.test/sample-desktop-steps.hpp>
#include <cstddef>
#include <cstdint>

namespace SampleGate {
class Sample : public SampleDesktopSteps {
protected:
  auto ThenTouchEvent(Client& client, std::string_view event, std::string_view detail) -> void;
  auto ThenIgnoredWarpMotion(Client& client, rdpInput* input, std::uint16_t x, std::uint16_t y, char const* delta)
      -> void;
  auto GivenRelativeOrigin(Client const& client)                                       -> void;
  auto WhenUnicodeControl(rdpInput* input, int code)                                   -> void;
  auto ThenStoppedScancodeText(std::size_t stopped)                                    -> void;
  auto WhenReverseWheel(Client& client, auto* advanced)                                -> void {
    ASSERT_EQ(advanced->AInputSendInputEvent(advanced, AINPUT_FLAGS_WHEEL, -120 * 65536, 120 * 65536), CHANNEL_RC_OK);
    ASSERT_TRUE(ReadInput(client, "event MOUSE_WHEEL "));
    EXPECT_TRUE(line.ends_with(" x=-1 y=1")) << line;
  }
  auto WhenAspectRelative(Client& client)                                                              -> void;
  auto WhenAdvancedMotion(Client& client)                                                              -> void;
  auto WhenRelativeAdvanced(Client& client, std::int32_t x, std::int32_t y, std::string_view expected) -> void;
  auto ThenWarpEchoIgnored(rdpInput* input)                                                            -> void;
  auto WhenRelativeWarp(Client& client, rdpInput* input)                                               -> void;
  auto WhenPreciseWheel(rdpInput* input, std::uint16_t flags, char const* expected)                    -> void;
  auto ThenStoppedUnicode(rdpInput* input)                                                             -> void;
  auto GivenFrenchKeyboard(Client const& client)                                                       -> void;
};
}
