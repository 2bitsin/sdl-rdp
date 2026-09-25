#pragma once
#include "observer.hpp"
#include <sdl-rdp/headless-client.test/frame/checks.hpp>

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace sdl_rdp::headless_client_test::graphics::detail::session {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::frame::FrameChecks;

class GraphicsSession : public testing::Test, protected FrameChecks {
protected:
  auto ThenWriteDisconnect(Client& client)                                        -> void;
  auto Open(std::uint32_t w = 640, std::uint32_t h = 480, sdlrdp_aspect aspect = { },
            sdlrdp_codec codec = SDLRDP_CODEC_RAW, std::uint32_t audio_latency = 0) -> void;
  auto GraphicsClient()                                                           -> Client&;
  auto Observer()                                                                 -> GraphicsObserver&;
  auto PresentProgressiveDamage(Client& client, std::vector<std::uint32_t> const& pixels, sdlrdp_rect damage) -> void;
  auto ConnectPipeline(Client& client)                                            -> void;
  auto GivenGraphicsClient(sdlrdp_codec codec)                                    -> void;
  auto GivenPipelinedGraphics()                                                   -> void;
  auto ThenLegacyFallback(Client& client)                                         -> void;
  auto PresentMatching(Client& client, std::vector<std::uint32_t> const& pixels)  -> void;
  auto ShowFirstPicture(Client& client, std::vector<std::uint32_t> const& pixels) -> void;
  auto AwaitFrames(Client& client, auto const& frames, std::size_t count)         -> void {
    ASSERT_TRUE(client.Until([&] { return frames.size() == count; }));
  }
  auto PresentGraphicsFrames(Client& client, GraphicsObserver& observer, std::vector<std::uint32_t> const& pixels,
                             std::uint32_t first, std::uint32_t last) -> void;
  auto ConnectGraphics(Client& client, GraphicsObserver& observer) -> void;
  auto Connect(Client& client, bool ack = true)                    -> void;
  std::unique_ptr<Client>           graphics_client;
  std::unique_ptr<GraphicsObserver> graphics_observer;
};
}

namespace sdl_rdp::headless_client_test::graphics {
using detail::session::GraphicsSession;
}
