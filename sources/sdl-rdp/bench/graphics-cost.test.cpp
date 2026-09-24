#include <sdl-rdp/headless-client.test/graphics-cost.hpp>
#include <sdl-rdp/headless-client.test/await-acknowledged.hpp>
#include <sdl-rdp/headless-client.test/backend-instance.hpp>
#include <sdl-rdp/video/avc-encoder.hpp>

#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace {
using Headless::GraphicsCost;
TEST_F(GraphicsCost, FullRandomFrame) {
  ASSERT_NO_FATAL_FAILURE(Open());
  Headless::Client client(sdlrdp_port(backend.Handle()), true, 1280, 800);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(client.Connect());
  EXPECT_EQ(client.Instance()->context->codecs->ThreadingFlags, THREADING_FLAGS_DISABLE_THREADS);
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  std::vector<std::uint32_t> pixels(1280uz * 800);
  std::mt19937               random(17);            // NOLINT(cert-msc32-c, cert-msc51-cpp): Reproducible codec input.
  std::ranges::generate(pixels, [&] { return random() & 0x00ffffff; });
  sdlrdp_rect const full{ 0, 0, 1280, 800 };
  ASSERT_EQ(backend.Present(pixels, 1280, 800, full), 0);
  ASSERT_NO_FATAL_FAILURE(sdl_rdp::headless_client_test::AwaitAllAcknowledged(client, backend, logs));
  ThenProgressiveCost(client, observer);
}
TEST_F(GraphicsCost, AvcFullFrame) {
  if (!Backend::Avc::Encoder::Available()) GTEST_SKIP() << Backend::Avc::Encoder::UnavailableReason();
  ASSERT_NO_FATAL_FAILURE(Open(1920, 1080, SDLRDP_CODEC_AVC420));
  Headless::Client client(sdlrdp_port(backend.Handle()), true, 1920, 1080);
  client.EnableGraphics(true);
  Headless::GraphicsObserver observer(client);
  ASSERT_NO_FATAL_FAILURE(ConnectGraphics(client));
  ASSERT_NO_FATAL_FAILURE(PresentMovingTiles(client, 10));
  ASSERT_EQ(observer.Observed().avc_nals.size(), 10u);
  ASSERT_EQ(observer.Observed().frames.size(), 10u);
  client.Disconnect();
  backend.Close();
  RecordAvcCost(logs);
}
}
