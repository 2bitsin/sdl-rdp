#pragma once
#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/link/event.hpp>

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
namespace sdl_rdp::headless_client_test::input::detail::steps {
using sdl_rdp::headless_client_test::backend::As;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Tap;
using sdl_rdp::link::Event;
using sdl_rdp::link::Key;
using sdl_rdp::link::MouseButton;

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
inline auto ThenKey(Event const& event, bool down) -> void {
  auto const& key = As<Key>(event);
  EXPECT_EQ(key.scancode, 0x1Eu);
  EXPECT_FALSE(key.extended);
  EXPECT_EQ(key.down, down);
}
inline auto ThenKeyboard(std::span<Event const> events) -> void {
  for (std::size_t i = 0; i < 2; ++i) {
    ThenKey(events[i], !i);
  }
}
inline auto ThenMouseButtons(std::span<Event const> events) -> void {
  for (std::size_t i = 3; i < 5; ++i) {
    auto const& button = As<MouseButton>(events[i]);
    EXPECT_EQ(button.button, 1u);
    EXPECT_EQ(button.down, i == 3);
  }
}
}

namespace sdl_rdp::headless_client_test::input {
using detail::steps::SendKeyboardAndMouse;
using detail::steps::ThenKeyboard;
using detail::steps::ThenMouseButtons;
}
