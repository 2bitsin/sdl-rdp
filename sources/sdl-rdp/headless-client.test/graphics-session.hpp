#pragma once
#include "frame-checks.hpp"
#include "graphics-observer.hpp"

#include <gtest/gtest.h>
#include <cstddef>
#include <memory>
#include <vector>

namespace BackendGate {
class GraphicsSession : public testing::Test, protected FrameChecks {
protected:
  auto ThenWriteDisconnect(Client& client)                                        -> void;
  auto Open(unsigned w = 640, unsigned h = 480, sdlrdp_aspect aspect = { }, sdlrdp_codec codec = SDLRDP_CODEC_RAW,
            unsigned audio_latency = 0) -> void;
  auto GraphicsClient()                                                           -> Client&;
  auto GraphicsObserver()                                                         -> Headless::GraphicsObserver&;
  auto PresentProgressiveDamage(Client& client, std::vector<UINT32> const& pixels, sdlrdp_rect damage) -> void;
  auto ConnectPipeline(Client& client)                                            -> void;
  auto GivenGraphicsClient(sdlrdp_codec codec)                                    -> void;
  auto GivenPipelinedGraphics()                                                   -> void;
  auto ThenLegacyFallback(Client& client)                                         -> void;
  auto PresentMatching(Client& client, std::vector<std::uint32_t> const& pixels)  -> void;
  auto ShowFirstPicture(Client& client, std::vector<std::uint32_t> const& pixels) -> void;
  auto AwaitFrames(Client& client, auto const& frames, std::size_t count)         -> void {
    ASSERT_TRUE(client.Until([&] { return frames.size() == count; }));
  }
  auto PresentGraphicsFrames(Client& client, Headless::GraphicsObserver& observer, std::vector<UINT32> const& pixels,
                             unsigned first, unsigned last) -> void;
  auto ConnectGraphics(Client& client, Headless::GraphicsObserver& observer) -> void;
  auto Connect(Client& client, bool ack = true)                              -> void;
  std::unique_ptr<Client>                     graphics_client;
  std::unique_ptr<Headless::GraphicsObserver> graphics_observer;
};
}
