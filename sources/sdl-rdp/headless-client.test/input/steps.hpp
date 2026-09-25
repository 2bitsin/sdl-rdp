#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/client/client.hpp>

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
namespace Headless {
inline auto SendMouse(Client& client, std::uint16_t x, std::uint16_t y) -> void {
  auto* input = client.Instance()->context->input;
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, x, y)) << "send motion";
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON1 | PTR_FLAGS_DOWN, x, y)) << "send left down";
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON1, x, y)) << "send left up";
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_WHEEL | 120, 0, 0)) << "send wheel";
}
inline auto SendKeyboardAndMouse(Client& client, std::uint16_t x, std::uint16_t y) -> void {
  ASSERT_NO_FATAL_FAILURE(Tap(client, 0x1e));
  SendMouse(client, x, y);
}
inline auto ThenKey(sdlrdp_event const& event, bool down) -> void {
  EXPECT_EQ(event.type, SDLRDP_KEY);
  EXPECT_EQ(event.key.scancode, 0x1Eu);
  EXPECT_EQ(event.key.extended, 0);
  EXPECT_EQ(event.key.down, down);
}
inline auto ThenKeyboard(std::span<sdlrdp_event const> events) -> void {
  for (std::size_t i = 0; i < 2; ++i) {
    ThenKey(events[i], !i);
  }
}
inline auto ThenMouseButtons(std::span<sdlrdp_event const> events) -> void {
  for (std::size_t i = 3; i < 5; ++i) {
    EXPECT_EQ(events[i].type, SDLRDP_MOUSE_BUTTON);
    EXPECT_EQ(events[i].mouse_button.button, 1u);
    EXPECT_EQ(events[i].mouse_button.down, i == 3);
  }
}
}
