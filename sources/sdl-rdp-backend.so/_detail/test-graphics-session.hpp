#pragma once
#include "graphics-observer.hpp"
#include "test-frame-checks.hpp"

#include <cstddef>
#include <gtest/gtest.h>
#include <memory>
#include <vector>

namespace BackendGate {
class GraphicsSession : public testing::Test, protected FrameChecks {
protected:
  void                        ThenWriteDisconnect(Client& client);
  void Open(unsigned w = 640, unsigned h = 480, sdlrdp_aspect aspect = { }, sdlrdp_codec codec = SDLRDP_CODEC_RAW,
            unsigned audio_latency = 0);
  Client&                     GraphicsClient();
  Headless::GraphicsObserver& GraphicsObserver();
  void PresentProgressiveDamage(Client& client, std::vector<UINT32> const& pixels, sdlrdp_rect damage);
  void                        ConnectPipeline(Client& client);
  void                        ThenProgressivePicture(Client& client, std::vector<UINT32> const& pixels);
  void                        GivenGraphicsClient(sdlrdp_codec codec);
  void                        GivenPipelinedGraphics();
  void                        ThenLegacyFallback(Client& client);
  void                        PresentMatching(Client& client, std::vector<UINT32> const& pixels);
  void                        AwaitFrames(Client& client, auto const& frames, std::size_t count) {
    ASSERT_TRUE(client.Until([&] { return frames.size() == count; }));
  }
  void PresentGraphicsFrames(Client& client, Headless::GraphicsObserver& observer, std::vector<UINT32> const& pixels,
                             unsigned first, unsigned last);
  void ConnectGraphics(Client& client, Headless::GraphicsObserver& observer);
  void Connect(Client& client, bool ack = true);
  std::unique_ptr<Client>                     graphics_client;
  std::unique_ptr<Headless::GraphicsObserver> graphics_observer;
};
}
