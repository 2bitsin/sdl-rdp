#include <sdl-rdp/sample-gate.test/sample-input.hpp>

#include <sdl-rdp/sample-gate.test/next-frame.hpp>

#include <freerdp/input.h>
#include <oxbox/utilities/number-text.hpp>
#include <sdl-rdp/headless-client.test/input-steps.hpp>
#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace SampleGate {
auto SampleInput::ThenInputEvent(std::string_view event, std::string_view text, std::uint32_t& motion_frame) -> void {
  ASSERT_TRUE(Read("event " + std::string(event) + " ")) << event << text << ": " << process->Transcript();
  ASSERT_TRUE(line.contains(text)) << "expected " << event << text << ", actual: " << line;
  if (event == "MOUSE_MOTION") {
    motion_frame = utilities::Required(oxbox::utilities::ParseNumberAfter<std::uint32_t>(line, " frame="),
                                       "motion events carry a frame identifier");
  }
}
auto SampleInput::GivenFocus(Client& client) -> void {
  ASSERT_TRUE(client.Connect());
  ASSERT_TRUE(Read("event FOCUS_GAINED "));
}
auto SampleInput::WhenTextStops(Client const& client) -> void {
  ASSERT_NO_FATAL_FAILURE(client.Tap(0x3c));
  ASSERT_TRUE(Read("event TEXT_MODE active=0"));
}
auto SampleInput::WhenRelative(Client const& client) -> void {
  ASSERT_NO_FATAL_FAILURE(client.Tap(0x3d));
  ASSERT_TRUE(Read("event RELATIVE_MODE active=1"));
}
auto SampleInput::WhenKeyDown(Client& client) -> void {
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x1e));
  ASSERT_TRUE(ReadInput(client, "event KEY_DOWN "));
}
auto SampleInput::ReadInput(Client& client, std::string_view expected, std::chrono::milliseconds timeout) -> bool {
  Expects(!expected.empty(), "expected input event supplied");
  bool received = false;
  return client.Until([&] { return received || (received = Read(expected, 1ms)); }, timeout);
}
auto SampleInput::Input(Client& client) -> void {
  std::uint32_t motion_frame = 0;
  ASSERT_NO_FATAL_FAILURE(Headless::SendKeyboardAndMouse(client, 100, 120));
  for (auto [event, text] :
       std::array<std::pair<std::string_view, std::string_view>, 6>{ { { "KEY_DOWN"         , " scancode=4 " },
                                                                       { "KEY_UP"           , " scancode=4 " },
                                                                       { "MOUSE_MOTION"     , " x=100 y=120" },
                                                                       { "MOUSE_BUTTON_DOWN", " button=1 "   },
                                                                       { "MOUSE_BUTTON_UP"  , " button=1 "   },
                                                                       { "MOUSE_WHEEL"      , " y=1"         } } }) {
    ASSERT_NO_FATAL_FAILURE(ThenInputEvent(event, text, motion_frame));
  }
  NextFrame frame(client, motion_frame);
  ASSERT_TRUE(client.Until([&] { return frame.Received(); })) << "next complete frame after motion";
  ASSERT_TRUE(frame.Matches()) << "frame excludes pointer: " << frame.Matches().message();
}
}
