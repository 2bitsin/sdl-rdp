#pragma once
#include <sdl-rdp/sample-gate.test/sample/desktop-steps.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace sdl_rdp::sample_gate_test::sample::detail::sample {
using sdl_rdp::headless_client_test::client::Client;

class Sample : public SampleDesktopSteps {
protected:
  auto ThenTouchEvent(Client& client, std::string_view event, std::string_view detail)                 -> void;
  auto ThenIgnoredWarpMotion(Client& client, rdpInput& input, std::uint16_t x, std::uint16_t y, std::string_view delta)
      -> void;
  auto GivenRelativeOrigin(Client& client)                                                             -> void;
  auto WhenUnicodeControl(rdpInput& input, int code)                                                   -> void;
  auto ThenStoppedScancodeText(std::size_t stopped)                                                    -> void;
  auto WhenReverseWheel(Client& client)                                                                -> void;
  auto WhenAspectRelative(Client& client)                                                              -> void;
  auto WhenAdvancedMotion(Client& client)                                                              -> void;
  auto WhenRelativeAdvanced(Client& client, std::int32_t x, std::int32_t y, std::string_view expected) -> void;
  auto ThenWarpEchoIgnored(rdpInput& input)                                                            -> void;
  auto WhenRelativeWarp(Client& client, rdpInput& input)                                               -> void;
  auto WhenPreciseWheel(rdpInput& input, std::uint16_t flags, std::string_view expected)               -> void;
  auto ThenStoppedUnicode(rdpInput& input)                                                             -> void;
  auto GivenFrenchKeyboard(Client& client)                                                             -> void;
};
}

namespace sdl_rdp::sample_gate_test::sample {
using detail::sample::Sample;
}
