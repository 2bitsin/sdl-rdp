#include <sdl-rdp/headless-client.test/graphics-cost.hpp>
#include <sdl-rdp/video/avc-encoder.hpp>

#include <gtest/gtest.h>
#include <algorithm>
#include <random>
#include <vector>

namespace {
using Headless::GraphicsCost;
TEST_F(GraphicsCost, FullRandomFrame) {
  Open();
  if (::testing::Test::HasFatalFailure()) return;
  Headless::Client client(sdlrdp_port(backend.get()), true, 1280, 800);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  EXPECT_EQ(client.Instance()->context->codecs->ThreadingFlags, THREADING_FLAGS_DISABLE_THREADS);
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  std::vector<UINT32> pixels(1280uz * 800);
  std::mt19937        random(17);            // NOLINT(cert-msc32-c, cert-msc51-cpp): Reproducible codec input.
  std::ranges::generate(pixels, [&] { return random() & 0x00ffffff; });
  sdlrdp_rect const full{ 0, 0, 1280, 800 };
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 5120, 1280, 800, &full, 1), 0);
  AwaitAcknowledgement(client, 1);
  if (::testing::Test::HasFatalFailure()) return;
  ThenProgressiveCost(client, observer);
}
TEST_F(GraphicsCost, AvcFullFrame) {
  if (!Backend::Avc::Encoder::Available()) GTEST_SKIP() << Backend::Avc::Encoder::UnavailableReason();
  Open(1920, 1080, SDLRDP_CODEC_AVC420);
  if (::testing::Test::HasFatalFailure()) return;
  Headless::Client client(sdlrdp_port(backend.get()), true, 1920, 1080);
  client.EnableGraphics(true);
  Headless::GraphicsObserver observer(client);
  ConnectGraphics(client);
  if (::testing::Test::HasFatalFailure()) return;
  PresentMovingTiles(client, 10);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_EQ(observer.Observed().avc_nals.size(), 10u);
  ASSERT_EQ(observer.Observed().frames.size(), 10u);
  freerdp_disconnect(client.Instance().get());
  backend.reset();
  RecordAvcCost(logs);
}
}
