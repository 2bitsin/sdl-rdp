#pragma once
#include "observer.hpp"
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/headless-client.test/frame/checks.hpp>
#include <sdl-rdp/utilities/geometry.hpp>

#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace sdl_rdp::headless_client_test::graphics::detail::session {
using sdl_rdp::configuration::Codec;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::frame::FrameChecks;
using sdl_rdp::utilities::AspectRatio;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Rect;

class GraphicsSession : public testing::Test, protected FrameChecks {
protected:
  auto ThenWriteDisconnect(Client& client)                                             -> void;
  auto Open(std::uint32_t w = 640, std::uint32_t h = 480, std::optional<AspectRatio> aspect = { },
            Codec codec = Codec::Raw, std::uint32_t audio_latency = 0) -> void;
  auto GraphicsClient()                                                                -> Client&;
  auto Observer()                                                                      -> GraphicsObserver&;
  auto PresentProgressiveDamage(Client& client, Pixels const& pixels, Rect damage)     -> void;
  auto ConnectPipeline(Client& client)                                                 -> void;
  auto GivenGraphicsClient(Codec codec, Extent size = { .width = 640, .height = 480 }) -> void;
  auto GivenPipelinedGraphics()                                                        -> void;
  auto ThenLegacyFallback(Client& client)                                              -> void;
  auto PresentMatching(Client& client, Pixels const& pixels)                           -> void;
  auto ShowFirstPicture(Client& client, Pixels const& pixels)                          -> void;
  auto AwaitFrames(Client& client, auto const& frames, std::size_t count)              -> void {
    ASSERT_TRUE(client.Until([&] { return frames.size() == count; }));
  }
  auto PresentGraphicsFrames(Client& client, GraphicsObserver& observer, Pixels const& pixels, std::uint32_t first,
                             std::uint32_t last) -> void;
  auto ConnectConfirmed(Client& client)         -> void;
  auto Connect(Client& client, bool ack = true) -> void;
  std::unique_ptr<Client>           graphics_client;
  std::unique_ptr<GraphicsObserver> graphics_observer;
};
}

namespace sdl_rdp::headless_client_test::graphics {
using detail::session::GraphicsSession;
}
