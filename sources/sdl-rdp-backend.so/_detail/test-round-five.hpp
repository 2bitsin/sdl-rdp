#pragma once
#include "test-graphics-session.hpp"

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
  void ThenPipelinedWindow(Client& client, auto const& frames, std::vector<UINT32> const& pixels) {
    Present(pixels, 320, 200);
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
    AwaitFrames(client, frames, 1);
    if (::testing::Test::HasFatalFailure()) return;
    Present(pixels, 320, 200);
    AwaitFrames(client, frames, 2);
    if (::testing::Test::HasFatalFailure()) return;
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
  }
  void ThenNeverAcknowledges(auto timed) {
    Open(320, 200);
    EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
    Client client(sdlrdp_port(backend.get()), true);
    Connect(client);
    FrameObserver const       observer(client);
    std::vector<UINT32> const pixels(320uz * 200, 0x778899);
    timed([&] {
      Present(pixels, 320, 200);
      ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 1; }));
      ThenAcknowledgementTimeout(pixels);
    });
  }
  void ThenAcknowledgementTimeout(std::vector<UINT32> const& pixels);
  void ThenTimedOutFrames(std::string_view sent, unsigned minimum);
  void ThenAspectMouse(Client& client);
  void ThenAgedWindowResumes(std::vector<UINT32> const& pixels);
  void ThenColourDepth(unsigned depth);
  void WhenAcknowledgedFrame(Client& client, FrameObserver& observer, std::vector<UINT32> const& pixels, unsigned i,
                             auto wait) {
    Present(pixels, 320, 200);
    auto waiting = std::async(std::launch::async, wait, std::ref(*backend));
    ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == i; }));
    EXPECT_EQ(waiting.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
    ASSERT_TRUE(observer.Ack());
    ASSERT_EQ(waiting.get(), 1);
    EXPECT_TRUE(std::ranges::none_of(Events(), [](auto const& event) { return event.type == SDLRDP_REFRESH; }));
  }
  void ThenProgressiveDamageCost(Client& client, Headless::GraphicsObserver& observer, uint64_t before);
  void ThenAutoChangesToRaw(Client& client, std::vector<UINT32>& pixels);
  void ThenGraphicsTimeoutStatistics();
  void ThenGraphicsAcknowledgementsCounted();
  void ThenGraphicsWindowReleases(std::vector<UINT32> const& pixels);
  void ThenLegacyWindowReleases(Client& client, FrameObserver const& observer, std::vector<UINT32> const& pixels);
};
}
