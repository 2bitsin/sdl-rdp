#pragma once
#include "../sdl-rdp-backend.h"
#include "client.hpp"

#include <gtest/gtest.h>
namespace Headless {
inline void SendMouse(Client const& client, UINT16 x, UINT16 y) {
  auto* input = client.Instance()->context->input;
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, x, y)) << "send motion";
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON1 | PTR_FLAGS_DOWN, x, y)) << "send left down";
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON1, x, y)) << "send left up";
  ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_WHEEL | 120, 0, 0)) << "send wheel";
}
inline void SendKeyboardAndMouse(Client const& client, UINT16 x, UINT16 y) {
  auto* input = client.Instance()->context->input;
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x1e)) << "send A down";
  ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x1e)) << "send A up";
  SendMouse(client, x, y);
}
inline void ThenKey(sdlrdp_event const& event, bool down) {
  EXPECT_EQ(event.type, SDLRDP_KEY);
  EXPECT_EQ(event.key.scancode, 0x1Eu);
  EXPECT_EQ(event.key.extended, 0);
  EXPECT_EQ(event.key.down, down);
}
inline void ThenKeyboard(std::span<sdlrdp_event const> events) {
  for (unsigned i = 0; i < 2; ++i) {
    ThenKey(events[i], !i);
  }
}
inline void ThenMouseButtons(std::span<sdlrdp_event const> events) {
  for (unsigned i = 3; i < 5; ++i) {
    EXPECT_EQ(events[i].type, SDLRDP_MOUSE_BUTTON);
    EXPECT_EQ(events[i].mouse_button.button, 1u);
    EXPECT_EQ(events[i].mouse_button.down, i == 3);
  }
}
}
