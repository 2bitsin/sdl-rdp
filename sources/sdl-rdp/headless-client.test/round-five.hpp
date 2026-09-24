#pragma once
#include "graphics-session.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <string_view>
#include <vector>

namespace BackendGate {
class RoundFive : public GraphicsSession {
protected:
  auto ThenPipelinedWindow(Client& client, auto const& frames, std::vector<UINT32> const& pixels) -> void {
    ASSERT_NO_FATAL_FAILURE(Present(pixels, 320, 200));
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
    ASSERT_NO_FATAL_FAILURE(AwaitFrames(client, frames, 1));
    ASSERT_NO_FATAL_FAILURE(Present(pixels, 320, 200));
    ASSERT_NO_FATAL_FAILURE(AwaitFrames(client, frames, 2));
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
  }
  auto ThenNeverAcknowledges(auto timed) -> void {
    ASSERT_NO_FATAL_FAILURE(Open(320, 200));
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
    Client client(sdlrdp_port(backend.get()), true);
    ASSERT_NO_FATAL_FAILURE(Connect(client));
    FrameObserver const       observer(client);
    std::vector<UINT32> const pixels(320uz * 200, 0x778899);
    timed([&] {
      ASSERT_NO_FATAL_FAILURE(Present(pixels, 320, 200));
      ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 1; }));
      ThenAcknowledgementTimeout(pixels);
    });
  }
  auto ThenAcknowledgementTimeout(std::vector<UINT32> const& pixels) -> void;
  auto ThenTimedOutFrames(std::string_view sent, unsigned minimum)   -> void;
  auto ThenAspectMouse(Client& client)                               -> void;
  auto ThenAgedWindowResumes(std::vector<UINT32> const& pixels)      -> void;
  auto ThenColourDepth(unsigned depth)                               -> void;
  auto WhenAcknowledgedFrame(Client& client, FrameObserver& observer, std::vector<UINT32> const& pixels, unsigned i,
                             auto wait) -> void {
    ASSERT_NO_FATAL_FAILURE(Present(pixels, 320, 200));
    auto waiting = std::async(std::launch::async, wait, std::ref(*backend));
    ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == i; }));
    EXPECT_EQ(waiting.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
    ASSERT_TRUE(observer.Ack());
    ASSERT_EQ(waiting.get(), 1);
    EXPECT_TRUE(std::ranges::none_of(backend.Poll(), [](auto const& event) { return event.type == SDLRDP_REFRESH; }));
  }
  auto ThenProgressiveDamageCost(Client& client, Headless::GraphicsObserver& observer, uint64_t before) -> void;
  auto ThenAutoChangesToRaw(Client& client, std::vector<UINT32>& pixels)                                -> void;
  auto ThenGraphicsTimeoutStatistics()                                                                  -> void;
  auto ThenGraphicsAcknowledgementsCounted()                                                            -> void;
  auto ThenGraphicsWindowReleases(std::vector<UINT32> const& pixels)                                    -> void;
  auto ThenLegacyWindowReleases(Client& client, FrameObserver const& observer, std::vector<UINT32> const& pixels)
      -> void;
  auto RunPictureSizes(bool graphics)                                                                   -> void;
};
}
