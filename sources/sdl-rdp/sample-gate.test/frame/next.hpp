#pragma once
#include <gtest/gtest.h>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/headless-client.test/frame/update-hook.hpp>
#include <cstdint>

namespace sdl_rdp::sample_gate_test::frame::detail::next {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::frame::PictureUpdate;
using sdl_rdp::headless_client_test::frame::PictureUpdateHook;

class NextFrame {
public:
  explicit NextFrame(Client& value, std::uint32_t frame);
  auto     Received() const -> bool;
  auto     Matches() const  -> testing::AssertionResult const&;

private:
  auto Observe(PictureUpdate const& update) -> void;
  auto Inspect()                            -> void;
  bool                     received = false;
  testing::AssertionResult matches  = testing::AssertionFailure() << "no complete frame";
  Client&                  client;
  std::uint32_t            column;
  PictureUpdateHook        hook;
};
}

namespace sdl_rdp::sample_gate_test::frame {
using detail::next::NextFrame;
}
