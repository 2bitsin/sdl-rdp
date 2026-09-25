#pragma once
#include <sdl-rdp/sample-gate.test/sample/process.hpp>

#include <chrono>
#include <cstdint>
#include <string_view>

namespace sdl_rdp::sample_gate_test::sample::detail::input {
using sdl_rdp::headless_client_test::client::Client;
using std::chrono_literals::operator""s;

class SampleInput : public SampleProcess {
protected:
  auto ThenInputEvent(std::string_view event, std::string_view text, std::uint32_t& motion_frame)    -> void;
  auto GivenFocus(Client& client)                                                                    -> void;
  auto WhenTextStops(Client& client)                                                                 -> void;
  auto WhenRelative(Client& client)                                                                  -> void;
  auto WhenKeyDown(Client& client)                                                                   -> void;
  auto ReadInput(Client& client, std::string_view expected, std::chrono::milliseconds timeout = 10s) -> bool;
  auto Input(Client& client)                                                                         -> void;
};
}

namespace sdl_rdp::sample_gate_test::sample {
using detail::input::SampleInput;
}
