#include "_detail/gfx-protocol.hpp"
#include "_detail/headless-gfx.hpp"
#include "_detail/headless-tls.hpp"
#include "_detail/state.hpp"
#include "_detail/test-graphics.hpp"
#include "_detail/test-pattern.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <freerdp/primitives.h>
#include <gtest/gtest.h>

namespace {
std::vector<BYTE> PadReference(std::vector<UINT32> const& pixels, unsigned width, unsigned height, unsigned w,
                               unsigned h) {
  auto padded{ std::vector<BYTE>(std::size_t(w) * h * 4) };
  std::ranges::for_each(std::views::iota(0u, h), [&](unsigned y) {
    std::ranges::for_each(std::views::iota(0u, w), [&](unsigned x) {
      auto pixel = pixels[(std::min(y, height - 1) * width) + std::min(x, width - 1)];
      std::memcpy(padded.data() + ((std::size_t(y) * w + x) * 4), &pixel, 4);
    });
  });
  return padded;
}
std::vector<UINT32> Yuv420Reference(std::vector<UINT32> const& pixels, unsigned width, unsigned height) {
  auto w      { Backend::Avc::Aligned(width)                  };
  auto h      { Backend::Avc::Aligned(height)                 };
  auto padded = PadReference(pixels, width, height, w, h);
  auto yuv    { std::vector<BYTE>(std::size_t(w) * h * 3 / 2) };
  std::array<BYTE*, 3> planes{ yuv.data(), yuv.data() + (std::size_t(w) * h),
                               yuv.data() + (std::size_t(w) * h * 5 / 4) };
  std::array<UINT32, 3> strides{ w, w / 2, w / 2 };
  prim_size_t const     size   { w, h            };
  EXPECT_EQ(primitives_get()->RGBToYUV420_8u_P3AC4R(padded.data(), PIXEL_FORMAT_BGRX32, w * 4, planes.data(),
                                                    strides.data(), &size),
            0);
  std::array<BYTE const*, 3> source { planes[0], planes[1], planes[2]         };
  auto                       decoded{ std::vector<UINT32>(std::size_t(w) * h) };
  auto                       cropped{ std::vector<UINT32>(pixels.size())      };
  EXPECT_EQ(primitives_get()->YUV420ToRGB_8u_P3AC4R(source.data(), strides.data(),
                                                    reinterpret_cast<BYTE*>(decoded.data()), w * 4, PIXEL_FORMAT_BGRX32,
                                                    &size),
            0);
  std::ranges::for_each(std::views::iota(0u, height), [&](unsigned y) {
    std::copy_n(decoded.data() + (std::size_t(y) * w), width, cropped.data() + (std::size_t(y) * width));
  });
  return cropped;
}
void ThenScaledError(Headless::Client& client, std::vector<UINT32> const& scaled,
                     std::vector<UINT32> const& reference) {
  auto error = client.MaxError(scaled, &reference);
  std::cout << "Scaled maximum channel error: " << error << '\n';
  testing::Test::RecordProperty("scaled_maximum_channel_error", error);
  EXPECT_LE(error, 8u);
}
void RequireAvc() {
  if (!Backend::Avc::Encoder::Available()) GTEST_SKIP() << Backend::Avc::Encoder::UnavailableReason();
}
class AvcSession : public Headless::GraphicsBackend {
protected:
  void GivenAvc() {
    RequireAvc();
    if (::testing::Test::IsSkipped()) return;
    GivenGraphics(SDLRDP_CODEC_AVC420);
  }
  void GivenAutoFrame(bool avc = true) {
    GivenGraphics(SDLRDP_CODEC_AUTO, 320, 200, avc);
    if (::testing::Test::HasFatalFailure()) return;
    PresentFrame(std::vector<UINT32>(320uz * 200, 0x55aaff));
  }
  void PresentFrame(std::vector<UINT32> const& pixels, unsigned width = 320, unsigned height = 200) {
    Frame(ClientSession(), ObserverSession(), pixels, width, height, { 0, 0, int(width), int(height) });
  }
  void Open(sdlrdp_codec codec = SDLRDP_CODEC_AVC420, unsigned width = 320, unsigned height = 200) {
    auto pattern = std::to_array("/tmp/sdlrdp-avc-XXXXXX");
    OpenGraphics(pattern.data(), width, height, codec);
  }
  Headless::Client&           ClientSession()       { return *graphics_client; }
  Headless::GraphicsObserver& ObserverSession()     { return *graphics_observer; }
  void                        ThenProgressiveOnly() {
    EXPECT_TRUE(ObserverSession().Observed().avc_nals.empty());
    EXPECT_EQ(ObserverSession().Observed().progressive_headers, 1u);
  }
  void GivenGraphics(sdlrdp_codec codec = SDLRDP_CODEC_AVC420, unsigned width = 320, unsigned height = 200,
                     bool avc = true) {
    Open(codec, width, height);
    if (::testing::Test::HasFatalFailure()) return;
    graphics_client = std::make_unique<Headless::Client>(sdlrdp_port(backend.get()), true);
    graphics_client->EnableGraphics(avc);
    if (!avc)
      ASSERT_TRUE(freerdp_settings_set_bool(graphics_client->Instance()->context->settings, FreeRDP_GfxH264, FALSE));
    graphics_observer = std::make_unique<Headless::GraphicsObserver>(*graphics_client);
    ConnectGraphics(*graphics_client);
  }
  void ThenReported(Headless::Client& client, sdlrdp_codec codec) {
    bool reported = false;
    ASSERT_TRUE(client.Until([&] {
      std::array<sdlrdp_event, 32> events { };
      auto                         count  = sdlrdp_poll(backend.get(), events.data(), events.size());
      reported |= std::ranges::any_of(std::span(events).first(count), [=](auto const& event) {
        return (event.type == SDLRDP_CONNECTED && event.connected.codec == codec) ||
               (event.type == SDLRDP_CODEC_CHANGED && event.codec_changed.codec == codec);
      });
      return reported;
    }));
  }
  static void ReadScaledPixels(Headless::Client& client, std::vector<UINT32>& scaled) {
    auto* gdi = client.Instance()->context->gdi;
    ASSERT_EQ(gdi->width, 321);
    ASSERT_EQ(gdi->height, 214);
    std::ranges::for_each(std::views::iota(0, 214), [&](int y) {
      std::copy_n(reinterpret_cast<UINT32 const*>(gdi->primary_buffer + (std::size_t(y) * gdi->stride)), 321,
                  scaled.data() + (std::size_t(y) * 321));
    });
  }

  void Frame(Headless::Client& client, Headless::GraphicsObserver& observer, std::vector<UINT32> const& pixels,
             unsigned width, unsigned height, sdlrdp_rect damage) {
    auto before    { observer.Observed().frames.size()   };
    auto avc_before{ observer.Observed().avc_nals.size() };
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), width * 4, width, height, &damage, 1), 0);
    ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() > before; })) << logs.Text(true);
    auto reference = observer.Observed().avc_nals.size() > avc_before ? Yuv420Reference(pixels, width, height) : pixels;
    auto error     = client.MaxError(pixels, &reference);
    std::cout << "Graphics maximum channel error: " << error << '\n';
    RecordProperty("maximum_channel_error_" + std::to_string(observer.Observed().frames.size()), error);
    EXPECT_LE(error, 8u) << logs.Text(true);
    ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.get(), 0) == 1; }));
  }
  void TearDown() override {
    graphics_observer.reset();
    graphics_client.reset();
    GraphicsBackend::TearDown();
  }

  void ThenCodecChanged() {
    std::array<sdlrdp_event, 32> events  { };
    bool                         changed = false;
    ASSERT_TRUE(ClientSession().Until([&] {
      auto count = sdlrdp_poll(backend.get(), events.data(), events.size());
      for (unsigned i = 0; i < count; ++i)
        changed |= events[i].type == SDLRDP_CODEC_CHANGED && events[i].codec_changed.codec == SDLRDP_CODEC_PROGRESSIVE;
      return changed;
    }));
  }
  std::unique_ptr<Headless::Client>           graphics_client;
  std::unique_ptr<Headless::GraphicsObserver> graphics_observer;
};
class AvcGraphics : public AvcSession {
protected:
  void GivenSmallSurface(sdlrdp_codec codec, std::vector<UINT32> const& pixels) {
    GivenGraphics(codec, 32, 32);
    if (::testing::Test::HasFatalFailure()) return;
    PresentFrame(pixels, 32, 32);
    if (::testing::Test::HasFatalFailure()) return;
    ThenProgressiveOnly();
  }
  void WhenProgressiveSwitchesToAvc(std::vector<UINT32> const& pixels) {
    auto commands = ObserverSession().Observed().commands;
    ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_AVC420), 0);
    Frame(ClientSession(), ObserverSession(), pixels, 320, 200, { 18, 20, 8, 6 });
    if (::testing::Test::HasFatalFailure()) return;
    EXPECT_EQ(ObserverSession().Observed().commands, commands + 1);
    ASSERT_EQ(ObserverSession().Observed().avc_nals.size(), 1u);
    EXPECT_TRUE(ObserverSession().Observed().avc_nals.back() & (1u << 5));
  }
  void ThenFullSurfaceIdr() {
    ASSERT_EQ(ObserverSession().Observed().avc_nals.size(), 2u);
    EXPECT_TRUE(ObserverSession().Observed().avc_nals.back() & (1u << 5));
    ASSERT_FALSE(ObserverSession().Observed().avc_rects.empty());
    EXPECT_EQ(ObserverSession().Observed().avc_rects.back().right, 320);
    EXPECT_EQ(ObserverSession().Observed().avc_rects.back().bottom, 200);
  }
  void WhenSmallAvcRequested(std::vector<UINT32> const& pixels) {
    std::array<sdlrdp_event, 32> events{ };
    while (sdlrdp_poll(backend.get(), events.data(), events.size())) {
    }
    ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_AVC420), 0);
    PresentFrame(pixels, 32, 32);
    if (::testing::Test::HasFatalFailure()) return;
    ThenProgressiveOnly();
    if (::testing::Test::HasFatalFailure()) return;
    EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "surface below NVENC minimum"), 1u);
    EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "falls back"), 1u);
    ThenCodecChanged();
  }
  void ThenAvcResizes(std::vector<UINT32>& pixels) {
    pixels.assign(354uz * 226, 0x335577);
    PresentFrame(pixels, 354, 226);
    if (::testing::Test::HasFatalFailure()) return;
    ThenResizedIdr();
  }
  void ThenPFrameRegion() {
    ASSERT_EQ(ObserverSession().Observed().avc_rects.size(), 1u);
    ASSERT_EQ(ObserverSession().Observed().avc_quality.size(), 1u);
    EXPECT_EQ(ObserverSession().Observed().avc_rects[0].left, 18);
    EXPECT_EQ(ObserverSession().Observed().avc_rects[0].right, 26);
    ThenPFrameQuality();
  }
  void ThenScaledAvc(Headless::Client& client, Headless::GraphicsObserver& observer, std::vector<UINT32> const& pixels,
                     std::vector<UINT32> const& scaled, sdlrdp_rect full) {
    auto reference = Yuv420Reference(scaled, 321, 214);
    ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.get(), 0) == 1; }));
    ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_AVC420), 0);
    auto avc_before = observer.Observed().avc_nals.size();
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
    ASSERT_TRUE(client.Until([&] { return observer.Observed().avc_nals.size() > avc_before; }));
    ThenScaledError(client, scaled, reference);
    EXPECT_TRUE(observer.Observed().avc_nals.back() & (1u << 5));
  }
  void ThenResizedIdr() {
    EXPECT_EQ(ObserverSession().Observed().surfaces.size(), 2u);
    EXPECT_EQ(ObserverSession().Observed().commands, 3u);
    ASSERT_EQ(ObserverSession().Observed().avc_nals.size(), 3u);
    EXPECT_TRUE(ObserverSession().Observed().avc_nals.back() & (1u << 5));
    EXPECT_FALSE(logs.Contains("falls back")) << logs.Text(true);
  }
  void ThenPFrameQuality() {
    EXPECT_EQ(ObserverSession().Observed().avc_quality[0].qpVal, 0x9a);
    EXPECT_EQ(ObserverSession().Observed().avc_quality[0].qualityVal, 100);
  }
  void ScaledPattern(Headless::Client& client, Headless::GraphicsObserver& observer,
                     std::vector<UINT32> const& pixels) {
    ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_RAW), 0);
    ASSERT_EQ(sdlrdp_set_aspect(backend.get(), { 3, 2 }), 0);
    auto              before{ observer.Observed().frames.size() };
    sdlrdp_rect const full  { 0, 0, 320, 200                    };
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
    ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() > before; }));
    std::vector<UINT32> scaled(321uz * 214);
    ReadScaledPixels(client, scaled);
    if (::testing::Test::HasFatalFailure()) return;
    ThenScaledAvc(client, observer, pixels, scaled, full);
  }
  void ThenPFrameDamage() {
    ASSERT_EQ(ObserverSession().Observed().avc_nals.size(), 2u);
    EXPECT_EQ(ObserverSession().Observed().avc_nals[0] & ((1u << 5) | (1u << 7) | (1u << 8)),
              (1u << 5) | (1u << 7) | (1u << 8));
    EXPECT_TRUE(ObserverSession().Observed().avc_nals[1] & (1u << 1));
    EXPECT_FALSE(ObserverSession().Observed().avc_nals[1] & (1u << 5));
    ThenPFrameRegion();
  }
};

TEST_F(AvcGraphics, AutoWithAvcStartsWithIdr) {
  RequireAvc();
  if (::testing::Test::IsSkipped()) return;
  GivenAutoFrame();
  if (::testing::Test::HasFatalFailure()) return;
  ThenReported(ClientSession(), SDLRDP_CODEC_AVC420);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_EQ(ObserverSession().Observed().avc_nals.size(), 1u);
  EXPECT_TRUE(ObserverSession().Observed().avc_nals.front() & (1u << 5));
  EXPECT_EQ(ObserverSession().Observed().progressive_headers, 0u);
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "falls back"), 0u);
}
TEST_F(AvcGraphics, AutoWithoutAvcUsesProgressiveSilently) {
  GivenAutoFrame(false);
  if (::testing::Test::HasFatalFailure()) return;
  ThenReported(ClientSession(), SDLRDP_CODEC_PROGRESSIVE);
  if (::testing::Test::HasFatalFailure()) return;
  ThenProgressiveOnly();
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "falls back"), 0u);
}
TEST_F(AvcGraphics, DecodesPFrameAndResize) {
  GivenAvc();
  if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
  std::vector<UINT32> pixels(320uz * 200);
  constexpr std::array<UINT32, 4> colors{ 0xff0000, 0x00ff00, 0x0000ff, 0x55aaff };
  std::ranges::transform(std::views::iota(0uz, pixels.size()), pixels.begin(),
                         [&](size_t i) { return colors[(i % 320) / 80]; });
  PresentFrame(pixels);
  if (::testing::Test::HasFatalFailure()) return;
  sdlrdp_rect const damage{ 18, 20, 8, 6 };
  std::ranges::for_each(std::views::iota(20, 26),
                        [&](int y) { std::ranges::fill(std::span(pixels).subspan((y * 320) + 18, 8), 0x55aaffu); });
  Frame(ClientSession(), ObserverSession(), pixels, 320, 200, damage);
  if (::testing::Test::HasFatalFailure()) return;
  ThenPFrameDamage();
  if (::testing::Test::HasFatalFailure()) return;
  ThenAvcResizes(pixels);
}
TEST_F(AvcGraphics, ClientWithoutAvcFallsBackAndReportsChange) {
  GivenGraphics(SDLRDP_CODEC_AVC420, 320, 200, false);
  if (::testing::Test::HasFatalFailure()) return;
  std::vector<UINT32> const pixels(320uz * 200, 0x55aaff);
  PresentFrame(pixels);
  if (::testing::Test::HasFatalFailure()) return;
  ThenCodecChanged();
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_EQ(ObserverSession().Observed().progressive_headers, 1u);
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "AVC420 falls back"), 1u);
}
}

namespace {
TEST_F(AvcGraphics, SmallSurfaceFallsBack) {
  RequireAvc();
  if (::testing::Test::IsSkipped()) return;
  std::vector<UINT32> const pixels(32uz * 32, 0x55aaff);
  GivenSmallSurface(SDLRDP_CODEC_AVC420, pixels);
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "surface below NVENC minimum"), 1u);
}
TEST_F(AvcGraphics, AutoSmallSurfaceLogsFallbackWhenAvcRequested) {
  RequireAvc();
  if (::testing::Test::IsSkipped()) return;
  std::vector<UINT32> const pixels(32uz * 32, 0x55aaff);
  GivenSmallSurface(SDLRDP_CODEC_AUTO, pixels);
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_FALSE(logs.Contains("falls back"));
  EXPECT_FALSE(logs.Contains("surface below NVENC minimum"));
  WhenSmallAvcRequested(pixels);
}
TEST_F(AvcGraphics, ProgressiveConnectionSwitchesToAvcWithIdr) {
  RequireAvc();
  if (::testing::Test::IsSkipped()) return;
  GivenGraphics(SDLRDP_CODEC_PROGRESSIVE);
  if (::testing::Test::HasFatalFailure()) return;
  std::vector<UINT32> const pixels(320uz * 200, 0x335577);
  PresentFrame(pixels);
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_EQ(ObserverSession().Observed().progressive_headers, 1u);
  EXPECT_TRUE(ObserverSession().Observed().avc_nals.empty());
  WhenProgressiveSwitchesToAvc(pixels);
}
TEST_F(AvcGraphics, CodecSwitchRestoresFullSurfaceAndIdr) {
  GivenAvc();
  if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
  std::vector<UINT32> pixels(320uz * 200, 0xff0000);
  PresentFrame(pixels);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_RAW), 0);
  pixels.assign(pixels.size(), 0x335577);
  PresentFrame(pixels);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_AVC420), 0);
  Frame(ClientSession(), ObserverSession(), pixels, 320, 200, { 18, 20, 8, 6 });
  if (::testing::Test::HasFatalFailure()) return;
  ThenFullSurfaceIdr();
}
TEST_F(AvcGraphics, KnownPatternColours) {
  GivenAvc();
  if (::testing::Test::IsSkipped() || ::testing::Test::HasFatalFailure()) return;
  std::vector<UINT32> pixels(320uz * 200);
  Headless::MovingTilePattern(pixels, 320, 200, 0);
  PresentFrame(pixels);
  if (::testing::Test::HasFatalFailure()) return;
  EXPECT_EQ(ObserverSession().Observed().avc_nals.size(), 1u);
  ScaledPattern(ClientSession(), ObserverSession(), pixels);
}
TEST(AvcConfiguration, IntraRefreshUsesConfiguredFrameRate) {
  struct Case {
    unsigned fps;
    unsigned period;
    unsigned count;
  };
  constexpr std::array cases{
    Case{ .fps = 1, .period = 2, .count = 1 }, Case{ .fps = 24, .period = 48, .count = 12 },
    Case{ .fps = 30, .period = 60, .count = 15 }, Case{ .fps = 59, .period = 118, .count = 29 },
    Case{ .fps = 60, .period = 120, .count = 30 }, Case{ .fps = 144, .period = 288, .count = 72 }
  };
  std::ranges::for_each(cases, [](auto value) {
    auto refresh = Backend::Avc::IntraRefreshFor(value.fps);
    EXPECT_EQ(refresh.period, value.period);
    EXPECT_EQ(refresh.count, value.count);
  });
}

}
