#pragma once
#include <gtest/gtest.h>
#include <sdl-rdp-backend.so/_detail/client.hpp>
#include <sdl-rdp-backend.so/_detail/test-picture-update-hook.hpp>

namespace SampleGate {
class NextFrame {
public:
  explicit NextFrame(Headless::Client& value, unsigned frame);
  auto     Received() const -> bool;
  auto     Matches() const  -> testing::AssertionResult const&;

private:
  auto Observe(Headless::PictureUpdate const& update) -> void;
  auto Inspect()                                      -> void;
  bool                        received = false;
  testing::AssertionResult    matches  = testing::AssertionFailure() << "no complete frame";
  Headless::Client&           client;
  unsigned                    column;
  Headless::PictureUpdateHook hook;
};
}
