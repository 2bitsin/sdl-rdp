#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/headless-client.test/graphics/backend.hpp>
#include <sdl-rdp/headless-client.test/graphics/observer.hpp>
#include <sdl-rdp/picture/geometry.hpp>
#include <sdl-rdp/session/handle.hpp>
#include <sdl-rdp/utilities/extent.hpp>
#include <sdl-rdp/video/avc/encoder.hpp>
#include <sdl-rdp/video/avc/encoding.hpp>
#include <sdl-rdp/video/gfx/protocol.hpp>

#include <freerdp/primitives.h>
#include <gtest/gtest.h>
#include <oxbox/utilities/span.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>

namespace {
auto PadReference(std::vector<std::uint32_t> const& pixels, Backend::Extent size, Backend::Extent padded)
    -> std::vector<std::uint32_t> {
  auto const rows    = std::views::iota(0uz, std::size_t{ padded.height });
  auto const columns = std::views::iota(0uz, std::size_t{ padded.width });
  auto const edge    = [&](auto position) {
    auto const [y, x] = position;
    return pixels[(std::min<std::size_t>(y, size.height - 1) * size.width) + std::min<std::size_t>(x, size.width - 1)];
  };
  return std::views::cartesian_product(rows, columns) | std::views::transform(edge) | std::ranges::to<std::vector>();
}
template <typename Byte>
auto Yuv420Planes(std::span<Byte> yuv, Backend::Extent size) -> std::array<Byte*, 3> {
  auto const luma = std::size_t{ size.width } * size.height;
  return { yuv.data(), yuv.subspan(luma).data(), yuv.subspan(luma * 5 / 4).data() };
}
auto Yuv420Strides(Backend::Extent size) -> std::array<std::uint32_t, 3> {
  return { size.width, size.width / 2, size.width / 2 };
}
auto EncodeYuv420(std::vector<std::uint32_t> const& bgrx, Backend::Extent size) -> std::vector<std::uint8_t> {
  auto              yuv     { std::vector<std::uint8_t>(std::size_t{ size.width } * size.height * 3 / 2) };
  auto              planes  = Yuv420Planes(std::span(yuv), size);
  auto const        strides = Yuv420Strides(size);
  prim_size_t const area    { size.width, size.height                                                    };
  auto const        bytes   = oxbox::utilities::SpanCast<std::uint8_t const>(std::span(bgrx));
  EXPECT_EQ(primitives_get()->RGBToYUV420_8u_P3AC4R(bytes.data(), PIXEL_FORMAT_BGRX32, size.width * Backend::PixelBytes,
                                                    planes.data(), strides.data(), &area),
            0);
  return yuv;
}
auto DecodeYuv420(std::vector<std::uint8_t> const& yuv, Backend::Extent size) -> std::vector<std::uint32_t> {
  auto              decoded { std::vector<std::uint32_t>(std::size_t{ size.width } * size.height) };
  auto              planes  = Yuv420Planes(std::span(yuv), size);
  auto const        strides = Yuv420Strides(size);
  prim_size_t const area    { size.width, size.height                                             };
  EXPECT_EQ(primitives_get()->YUV420ToRGB_8u_P3AC4R(planes.data(), strides.data(),
                                                    oxbox::utilities::SpanCast<std::uint8_t>(std::span(decoded)).data(),
                                                    size.width * Backend::PixelBytes, PIXEL_FORMAT_BGRX32, &area),
            0);
  return decoded;
}
auto Cropped(std::span<std::uint32_t const> pixels, std::size_t stride, Backend::Extent size)
    -> std::vector<std::uint32_t> {
  return pixels | std::views::chunk(stride) | std::views::take(size.height)
         | std::views::transform([=](auto row) { return row | std::views::take(size.width); }) | std::views::join
         | std::ranges::to<std::vector>();
}
auto Yuv420Reference(std::vector<std::uint32_t> const& pixels, Backend::Extent size) -> std::vector<std::uint32_t> {
  Backend::Extent const aligned { .width  = Backend::Avc::Aligned(size.width),
                                  .height = Backend::Avc::Aligned(size.height) };
  auto const            yuv     = EncodeYuv420(PadReference(pixels, size, aligned), aligned);
  return Cropped(DecodeYuv420(yuv, aligned), aligned.width, size);
}
auto ThenScaledError(Headless::Client& client, std::vector<std::uint32_t> const& scaled,
                     std::vector<std::uint32_t> const& reference) -> void {
  auto error = client.MaxError(scaled, &reference);
  testing::Test::RecordProperty("scaled_maximum_channel_error", error);
  EXPECT_LE(error, 8u);
}
class AvcSession : public Headless::GraphicsBackend {
protected:
  auto GivenAutoFrame(bool avc = true) -> void {
    ASSERT_NO_FATAL_FAILURE(GivenGraphics(SDLRDP_CODEC_AUTO, 320, 200, avc));
    PresentFrame(std::vector<std::uint32_t>(320uz * 200, 0x55aaff));
  }
  auto PresentFrame(std::vector<std::uint32_t> const& pixels, std::uint32_t width = 320, std::uint32_t height = 200)
      -> void {
    Backend::Extent const size{ .width = width, .height = height };
    Frame(pixels, size, Backend::Whole(size));
  }
  auto Open(sdlrdp_codec codec = SDLRDP_CODEC_AVC420, std::uint32_t width = 320, std::uint32_t height = 200) -> void {
    auto pattern = std::to_array("/tmp/sdlrdp-avc-XXXXXX");
    OpenGraphics(pattern.data(), width, height, codec);
  }
  auto ClientSession() -> Headless::Client& {
    return *graphics_client;
  }
  auto ObserverSession() -> Headless::GraphicsObserver& {
    return *graphics_observer;
  }
  auto ThenProgressiveOnly() -> void {
    EXPECT_TRUE(ObserverSession().Observed().avc_nals.empty());
    EXPECT_EQ(ObserverSession().Observed().progressive_headers, 1u);
  }
  auto GivenGraphics(sdlrdp_codec codec = SDLRDP_CODEC_AVC420, std::uint32_t width = 320, std::uint32_t height = 200,
                     bool avc = true) -> void {
    ASSERT_NO_FATAL_FAILURE(Open(codec, width, height));
    graphics_client = std::make_unique<Headless::Client>(sdlrdp_port(backend.Handle()), true);
    graphics_client->EnableGraphics({ .h264 = avc });
    if (!avc)
      ASSERT_TRUE(freerdp_settings_set_bool(graphics_client->Instance()->context->settings, FreeRDP_GfxH264, false));
    graphics_observer = std::make_unique<Headless::GraphicsObserver>(*graphics_client);
    ConnectGraphics(*graphics_client);
  }
  auto ThenReported(Headless::Client& client, sdlrdp_codec codec) -> void {
    bool reported = false;
    ASSERT_TRUE(client.Until([&] {
      reported |= std::ranges::any_of(backend.Poll(), [=](auto const& event) {
        return (event.type == SDLRDP_CONNECTED && event.connected.codec == codec)
               || (event.type == SDLRDP_CODEC_CHANGED && event.codec_changed.codec == codec);
      });
      return reported;
    }));
  }
  static auto ReadScaledPixels(Headless::Client& client, std::vector<std::uint32_t>& scaled) -> void {
    auto const& gdi = *client.Instance()->context->gdi;
    ASSERT_EQ(gdi.width, 321);
    ASSERT_EQ(gdi.height, 214);
    auto const frame = std::span(gdi.primary_buffer, std::size_t{ gdi.stride } * 214);
    scaled = Cropped(oxbox::utilities::SpanCast<std::uint32_t const>(frame), gdi.stride / Backend::PixelBytes,
                     { .width = 321, .height = 214 });
  }

  auto Frame(std::vector<std::uint32_t> const& pixels, Backend::Extent size, sdlrdp_rect damage) -> void {
    auto& client     = ClientSession();
    auto& observer   = ObserverSession();
    auto  before     { observer.Observed().frames.size()   };
    auto  avc_before { observer.Observed().avc_nals.size() };
    ASSERT_EQ(backend.Present(pixels, size.width, size.height, damage), 0);
    ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() > before; })) << logs.Text(true);
    auto reference = observer.Observed().avc_nals.size() > avc_before ? Yuv420Reference(pixels, size) : pixels;
    auto error     = client.MaxError(pixels, &reference);
    RecordProperty("maximum_channel_error_" + std::to_string(observer.Observed().frames.size()), error);
    EXPECT_LE(error, 8u) << logs.Text(true);
    ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.Handle(), 0) == 1; }));
  }
  auto TearDown() -> void override {
    graphics_observer.reset();
    graphics_client.reset();
    GraphicsBackend::TearDown();
  }

  auto ThenCodecChanged() -> void {
    bool changed = false;
    ASSERT_TRUE(ClientSession().Until([&] {
      changed |= std::ranges::any_of(backend.Poll(), [](auto const& event) {
        return event.type == SDLRDP_CODEC_CHANGED && event.codec_changed.codec == SDLRDP_CODEC_PROGRESSIVE;
      });
      return changed;
    }));
  }
  std::unique_ptr<Headless::Client>           graphics_client;
  std::unique_ptr<Headless::GraphicsObserver> graphics_observer;
};
class AvcGraphics : public AvcSession {
protected:
  auto GivenSmallSurface(sdlrdp_codec codec, std::vector<std::uint32_t> const& pixels) -> void {
    ASSERT_NO_FATAL_FAILURE(GivenGraphics(codec, 32, 32));
    ASSERT_NO_FATAL_FAILURE(PresentFrame(pixels, 32, 32));
    ThenProgressiveOnly();
  }
  auto WhenProgressiveSwitchesToAvc(std::vector<std::uint32_t> const& pixels) -> void {
    auto commands = ObserverSession().Observed().commands;
    ASSERT_EQ(sdlrdp_set_codec(backend.Handle(), SDLRDP_CODEC_AVC420), 0);
    ASSERT_NO_FATAL_FAILURE(Frame(pixels, { .width = 320, .height = 200 }, { 18, 20, 8, 6 }));
    EXPECT_EQ(ObserverSession().Observed().commands, commands + 1);
    ASSERT_EQ(ObserverSession().Observed().avc_nals.size(), 1u);
    EXPECT_TRUE(ObserverSession().Observed().avc_nals.back() & (1u << 5));
  }
  auto ThenFullSurfaceIdr() -> void {
    ASSERT_EQ(ObserverSession().Observed().avc_nals.size(), 2u);
    EXPECT_TRUE(ObserverSession().Observed().avc_nals.back() & (1u << 5));
    ASSERT_FALSE(ObserverSession().Observed().avc_rects.empty());
    EXPECT_EQ(ObserverSession().Observed().avc_rects.back().right, 320);
    EXPECT_EQ(ObserverSession().Observed().avc_rects.back().bottom, 200);
  }
  auto WhenSmallAvcRequested(std::vector<std::uint32_t> const& pixels) -> void {
    while (!backend.Poll().empty()) {
    }
    ASSERT_EQ(sdlrdp_set_codec(backend.Handle(), SDLRDP_CODEC_AVC420), 0);
    ASSERT_NO_FATAL_FAILURE(PresentFrame(pixels, 32, 32));
    ASSERT_NO_FATAL_FAILURE(ThenProgressiveOnly());
    EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "surface below NVENC minimum"), 1u);
    EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "falls back"), 1u);
    ThenCodecChanged();
  }
  auto ThenAvcResizes(std::vector<std::uint32_t>& pixels) -> void {
    pixels.assign(354uz * 226, 0x335577);
    ASSERT_NO_FATAL_FAILURE(PresentFrame(pixels, 354, 226));
    ThenResizedIdr();
  }
  auto ThenPFrameRegion() -> void {
    ASSERT_EQ(ObserverSession().Observed().avc_rects.size(), 1u);
    ASSERT_EQ(ObserverSession().Observed().avc_quality.size(), 1u);
    EXPECT_EQ(ObserverSession().Observed().avc_rects[0].left, 18);
    EXPECT_EQ(ObserverSession().Observed().avc_rects[0].right, 26);
    ThenPFrameQuality();
  }
  auto ThenScaledAvc(Headless::Client& client, Headless::GraphicsObserver& observer,
                     std::vector<std::uint32_t> const& pixels, std::vector<std::uint32_t> const& scaled,
                     sdlrdp_rect full) -> void {
    auto reference = Yuv420Reference(scaled, { .width = 321, .height = 214 });
    ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.Handle(), 0) == 1; }));
    ASSERT_EQ(sdlrdp_set_codec(backend.Handle(), SDLRDP_CODEC_AVC420), 0);
    auto avc_before = observer.Observed().avc_nals.size();
    ASSERT_EQ(backend.Present(pixels, 320, 200, full), 0);
    ASSERT_TRUE(client.Until([&] { return observer.Observed().avc_nals.size() > avc_before; }));
    ThenScaledError(client, scaled, reference);
    EXPECT_TRUE(observer.Observed().avc_nals.back() & (1u << 5));
  }
  auto ThenResizedIdr() -> void {
    EXPECT_EQ(ObserverSession().Observed().surfaces.size(), 2u);
    EXPECT_EQ(ObserverSession().Observed().commands, 3u);
    ASSERT_EQ(ObserverSession().Observed().avc_nals.size(), 3u);
    EXPECT_TRUE(ObserverSession().Observed().avc_nals.back() & (1u << 5));
    EXPECT_FALSE(logs.Contains("falls back")) << logs.Text(true);
  }
  auto ThenPFrameQuality() -> void {
    EXPECT_EQ(ObserverSession().Observed().avc_quality[0].qpVal, 0x9a);
    EXPECT_EQ(ObserverSession().Observed().avc_quality[0].qualityVal, 100);
  }
  auto ScaledPattern(Headless::Client& client, Headless::GraphicsObserver& observer,
                     std::vector<std::uint32_t> const& pixels) -> void {
    ASSERT_EQ(sdlrdp_set_codec(backend.Handle(), SDLRDP_CODEC_RAW), 0);
    ASSERT_EQ(sdlrdp_set_aspect(backend.Handle(), { 3, 2 }), 0);
    auto              before{ observer.Observed().frames.size() };
    sdlrdp_rect const full  { 0, 0, 320, 200                    };
    ASSERT_EQ(backend.Present(pixels, 320, 200, full), 0);
    ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() > before; }));
    std::vector<std::uint32_t> scaled(321uz * 214);
    ASSERT_NO_FATAL_FAILURE(ReadScaledPixels(client, scaled));
    ThenScaledAvc(client, observer, pixels, scaled, full);
  }
  auto ThenPFrameDamage() -> void {
    ASSERT_EQ(ObserverSession().Observed().avc_nals.size(), 2u);
    EXPECT_EQ(ObserverSession().Observed().avc_nals[0] & ((1u << 5) | (1u << 7) | (1u << 8)),
              (1u << 5) | (1u << 7) | (1u << 8));
    EXPECT_TRUE(ObserverSession().Observed().avc_nals[1] & (1u << 1));
    EXPECT_FALSE(ObserverSession().Observed().avc_nals[1] & (1u << 5));
    ThenPFrameRegion();
  }
};
class AvcAvailable : public AvcGraphics {
protected:
  auto SetUp() -> void override {
    ASSERT_NO_FATAL_FAILURE(AvcGraphics::SetUp());
    if (!Backend::Avc::Encoder::Available()) GTEST_SKIP() << Backend::Avc::Encoder::UnavailableReason();
  }
};

TEST_F(AvcAvailable, AutoWithAvcStartsWithIdr) {
  ASSERT_NO_FATAL_FAILURE(GivenAutoFrame());
  ASSERT_NO_FATAL_FAILURE(ThenReported(ClientSession(), SDLRDP_CODEC_AVC420));
  ASSERT_EQ(ObserverSession().Observed().avc_nals.size(), 1u);
  EXPECT_TRUE(ObserverSession().Observed().avc_nals.front() & (1u << 5));
  EXPECT_EQ(ObserverSession().Observed().progressive_headers, 0u);
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "falls back"), 0u);
}
TEST_F(AvcGraphics, AutoWithoutAvcUsesProgressiveSilently) {
  ASSERT_NO_FATAL_FAILURE(GivenAutoFrame(false));
  ASSERT_NO_FATAL_FAILURE(ThenReported(ClientSession(), SDLRDP_CODEC_PROGRESSIVE));
  ASSERT_NO_FATAL_FAILURE(ThenProgressiveOnly());
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "falls back"), 0u);
}
TEST_F(AvcAvailable, DecodesPFrameAndResize) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphics(SDLRDP_CODEC_AVC420));
  std::vector<std::uint32_t>             pixels(320uz * 200);
  constexpr std::array<std::uint32_t, 4> colors{ 0xff0000, 0x00ff00, 0x0000ff, 0x55aaff };
  std::ranges::transform(std::views::iota(0uz, pixels.size()), pixels.begin(),
                         [&](std::size_t i) { return colors[(i % 320) / 80]; });
  ASSERT_NO_FATAL_FAILURE(PresentFrame(pixels));
  sdlrdp_rect const damage{ 18, 20, 8, 6 };
  std::ranges::for_each(std::views::iota(20, 26),
                        [&](int y) { std::ranges::fill(std::span(pixels).subspan((y * 320) + 18, 8), 0x55aaffu); });
  ASSERT_NO_FATAL_FAILURE(Frame(pixels, { .width = 320, .height = 200 }, damage));
  ASSERT_NO_FATAL_FAILURE(ThenPFrameDamage());
  ThenAvcResizes(pixels);
}
TEST_F(AvcGraphics, ClientWithoutAvcFallsBackAndReportsChange) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphics(SDLRDP_CODEC_AVC420, 320, 200, false));
  std::vector<std::uint32_t> const pixels(320uz * 200, 0x55aaff);
  ASSERT_NO_FATAL_FAILURE(PresentFrame(pixels));
  ASSERT_NO_FATAL_FAILURE(ThenCodecChanged());
  EXPECT_EQ(ObserverSession().Observed().progressive_headers, 1u);
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "AVC420 falls back"), 1u);
}
}

namespace {
TEST_F(AvcAvailable, SmallSurfaceFallsBack) {
  std::vector<std::uint32_t> const pixels(32uz * 32, 0x55aaff);
  ASSERT_NO_FATAL_FAILURE(GivenSmallSurface(SDLRDP_CODEC_AVC420, pixels));
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "surface below NVENC minimum"), 1u);
}
TEST_F(AvcAvailable, AutoSmallSurfaceLogsFallbackWhenAvcRequested) {
  std::vector<std::uint32_t> const pixels(32uz * 32, 0x55aaff);
  ASSERT_NO_FATAL_FAILURE(GivenSmallSurface(SDLRDP_CODEC_AUTO, pixels));
  EXPECT_FALSE(logs.Contains("falls back"));
  EXPECT_FALSE(logs.Contains("surface below NVENC minimum"));
  WhenSmallAvcRequested(pixels);
}
TEST_F(AvcAvailable, ProgressiveConnectionSwitchesToAvcWithIdr) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphics(SDLRDP_CODEC_PROGRESSIVE));
  std::vector<std::uint32_t> const pixels(320uz * 200, 0x335577);
  ASSERT_NO_FATAL_FAILURE(PresentFrame(pixels));
  EXPECT_EQ(ObserverSession().Observed().progressive_headers, 1u);
  EXPECT_TRUE(ObserverSession().Observed().avc_nals.empty());
  WhenProgressiveSwitchesToAvc(pixels);
}
TEST_F(AvcAvailable, CodecSwitchRestoresFullSurfaceAndIdr) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphics(SDLRDP_CODEC_AVC420));
  std::vector<std::uint32_t> pixels(320uz * 200, 0xff0000);
  ASSERT_NO_FATAL_FAILURE(PresentFrame(pixels));
  ASSERT_EQ(sdlrdp_set_codec(backend.Handle(), SDLRDP_CODEC_RAW), 0);
  pixels.assign(pixels.size(), 0x335577);
  ASSERT_NO_FATAL_FAILURE(PresentFrame(pixels));
  ASSERT_EQ(sdlrdp_set_codec(backend.Handle(), SDLRDP_CODEC_AVC420), 0);
  ASSERT_NO_FATAL_FAILURE(Frame(pixels, { .width = 320, .height = 200 }, { 18, 20, 8, 6 }));
  ThenFullSurfaceIdr();
}
TEST_F(AvcAvailable, KnownPatternColours) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphics(SDLRDP_CODEC_AVC420));
  std::vector<std::uint32_t> pixels(320uz * 200);
  Headless::MovingTilePattern(pixels, 320, 200, 0);
  ASSERT_NO_FATAL_FAILURE(PresentFrame(pixels));
  EXPECT_EQ(ObserverSession().Observed().avc_nals.size(), 1u);
  ScaledPattern(ClientSession(), ObserverSession(), pixels);
}
TEST(AvcConfiguration, IntraRefreshUsesConfiguredFrameRate) {
  struct Case {
    std::uint32_t fps;
    std::uint32_t period;
    std::uint32_t count;
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
