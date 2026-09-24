#pragma once
#include <gtest/gtest.h>
#include <sdl-rdp/headless-client.test/client.hpp>
#include <sdl-rdp/headless-client.test/picture-update-hook.hpp>
#include <cstdint>

namespace SampleGate {
class NextFrame {
public:
  explicit NextFrame(Headless::Client& value, std::uint32_t frame);
  auto     Received() const -> bool;
  auto     Matches() const  -> testing::AssertionResult const&;

private:
  auto Observe(Headless::PictureUpdate const& update) -> void;
  auto Inspect()                                      -> void;
  bool                        received = false;
  testing::AssertionResult    matches  = testing::AssertionFailure() << "no complete frame";
  Headless::Client&           client;
  std::uint32_t               column;
  Headless::PictureUpdateHook hook;
};
}
