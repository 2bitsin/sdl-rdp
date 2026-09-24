#pragma once
#include <sdl-rdp/sample-gate.test/sample-process.hpp>

#include <chrono>
#include <string_view>

namespace SampleGate {
class SampleInput : public SampleProcess {
protected:
  auto ThenInputEvent(std::string_view event, std::string_view text, std::uint32_t& motion_frame)    -> void;
  auto GivenFocus(Client& client)                                                                    -> void;
  auto WhenTextStops(Client const& client)                                                           -> void;
  auto WhenRelative(Client const& client)                                                            -> void;
  auto WhenKeyDown(Client& client)                                                                   -> void;
  auto ReadInput(Client& client, std::string_view expected, std::chrono::milliseconds timeout = 10s) -> bool;
  auto Input(Client& client)                                                                         -> void;
};
}
