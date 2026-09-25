#pragma once
#include "session.hpp"
#include <sdl-rdp/headless-client.test/frame/observer.hpp>
#include <sdl-rdp/session/backend.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace sdl_rdp::headless_client_test::graphics::detail::round_five {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::frame::FrameObserver;
using sdl_rdp::session::Backend;

using Wait = auto (&)(Backend& backend) -> bool;

class RoundFive : public GraphicsSession {
protected:
  auto ThenPipelinedWindow(Client& client, auto const& frames, Pixels const& pixels) -> void {
    ASSERT_NO_FATAL_FAILURE(Present(pixels, 320, 200));
    EXPECT_TRUE(backend.WaitFrame(std::chrono::milliseconds{ 0 }));
    ASSERT_TRUE(client.Until([&] { return frames.size() == 1; }));
    ASSERT_NO_FATAL_FAILURE(Present(pixels, 320, 200));
    ASSERT_TRUE(client.Until([&] { return frames.size() == 2; }));
    EXPECT_FALSE(backend.WaitFrame(std::chrono::milliseconds{ 1 }));
  }
  auto ThenNeverAcknowledges(auto timed) -> void {
    ASSERT_NO_FATAL_FAILURE(Open(320, 200));
    EXPECT_TRUE(backend.WaitFrame(std::chrono::milliseconds{ 0 }));
    Client client(backend.Port(), true);
    ASSERT_NO_FATAL_FAILURE(Connect(client));
    FrameObserver const observer(client);
    Pixels const        pixels(320uz * 200, 0x778899);
    timed([&] {
      ASSERT_NO_FATAL_FAILURE(Present(pixels, 320, 200));
      ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 1; }));
      ThenAcknowledgementTimeout(pixels);
    });
  }
  auto ThenAcknowledgementTimeout(Pixels const& pixels)                                              -> void;
  auto ThenTimedOutFrames(std::string_view sent, std::size_t minimum)                                -> void;
  auto ThenAspectMouse(Client& client)                                                               -> void;
  auto ThenAgedWindowResumes(Pixels const& pixels)                                                   -> void;
  auto ThenColourDepth(std::uint32_t depth)                                                          -> void;
  auto WhenAcknowledgedFrame(Client& client, FrameObserver& observer, Pixels const& pixels, std::size_t i, Wait wait)
      -> void;
  auto ThenProgressiveDamageCost(Client& client, GraphicsObserver& observer, std::uint64_t before)   -> void;
  auto ThenAutoChangesToRaw(Client& client, Pixels& pixels)                                          -> void;
  auto ThenGraphicsTimeoutStatistics()                                                               -> void;
  auto ThenGraphicsAcknowledgementsCounted()                                                         -> void;
  auto ThenGraphicsWindowReleases(Pixels const& pixels)                                              -> void;
  auto ThenLegacyWindowReleases(Client& client, FrameObserver const& observer, Pixels const& pixels) -> void;
  auto RunPictureSizes(bool graphics)                                                                -> void;
};
}

namespace sdl_rdp::headless_client_test::graphics {
using detail::round_five::RoundFive;
}
