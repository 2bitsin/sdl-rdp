#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/headless-client.test/graphics/backend.hpp>
#include <sdl-rdp/headless-client.test/graphics/observer.hpp>
#include <sdl-rdp/picture/geometry.hpp>
#include <sdl-rdp/session/backend.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
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

namespace sdl_rdp::integration::video_test::detail::gfx {
using sdl_rdp::configuration::Codec;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::freerdp_facade::BoolKey;
using sdl_rdp::headless_client_test::backend::Where;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::DecodedPixels;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::client::SettingsOf;
using sdl_rdp::headless_client_test::frame::FillArea;
using sdl_rdp::headless_client_test::frame::MovingTilePattern;
using sdl_rdp::headless_client_test::graphics::GraphicsBackend;
using sdl_rdp::headless_client_test::graphics::GraphicsObserver;
using sdl_rdp::link::CodecChanged;
using sdl_rdp::link::Connected;
using sdl_rdp::picture::Aligned;
using sdl_rdp::utilities::AspectRatio;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::PixelBytes;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::Whole;
using sdl_rdp::video::avc::Encoder;
using sdl_rdp::video::avc::IntraRefreshFor;
namespace {
auto PadReference(Pixels const& pixels, Extent size, Extent padded) -> Pixels {
  auto const rows    = std::views::iota(0uz, std::size_t{ padded.height });
  auto const columns = std::views::iota(0uz, std::size_t{ padded.width });
  auto const edge    = [&](auto position) {
    auto const [y, x] = position;
    return pixels[(std::min<std::size_t>(y, size.height - 1) * size.width) + std::min<std::size_t>(x, size.width - 1)];
  };
  return std::views::cartesian_product(rows, columns) | std::views::transform(edge) | std::ranges::to<std::vector>();
}
auto Luma(Extent size) -> std::size_t {
  return std::size_t{ size.width } * size.height;
}
auto Yuv420RoundTrip(Pixels const& bgrx, Extent size) -> Pixels {
  auto              yuv     { std::vector<std::uint8_t>(Luma(size) * 3 / 2)          };
  std::array        planes  { yuv.data(), &yuv[Luma(size)], &yuv[Luma(size) * 5 / 4] };
  std::array const  strides { size.width, size.width / 2, size.width / 2             };
  prim_size_t const area    { size.width, size.height                                };
  auto              decoded { Pixels(Luma(size))                                     };
  auto const&       convert = *primitives_get();
  // YUV420ToRGB takes const BYTE**, which the writable plane array does not convert to.
  std::array<std::uint8_t const*, 3> encoded{ planes[0], planes[1], planes[2] };
  EXPECT_EQ(convert.RGBToYUV420_8u_P3AC4R(oxbox::utilities::SpanCast<std::uint8_t const>(std::span(bgrx)).data(),
                                          PIXEL_FORMAT_BGRX32, size.width * PixelBytes, planes.data(), strides.data(),
                                          &area),
            0);
  EXPECT_EQ(convert.YUV420ToRGB_8u_P3AC4R(encoded.data(), strides.data(),
                                          oxbox::utilities::SpanCast<std::uint8_t>(std::span(decoded)).data(),
                                          size.width * PixelBytes, PIXEL_FORMAT_BGRX32, &area),
            0);
  return decoded;
}
auto Cropped(std::span<std::uint32_t const> pixels, std::size_t stride, Extent size) -> Pixels {
  return std::views::iota(std::size_t{ 0 }, std::size_t{ size.height })
         | std::views::transform([=](std::size_t row) { return pixels.subspan(row * stride, size.width); })
         | std::views::join | std::ranges::to<std::vector>();
}
auto Yuv420Reference(Pixels const& pixels, Extent size) -> Pixels {
  Extent const aligned{ .width = Aligned(size.width), .height = Aligned(size.height) };
  return Cropped(Yuv420RoundTrip(PadReference(pixels, size, aligned), aligned), aligned.width, size);
}
auto ThenScaledError(Client& client, Pixels const& scaled, Pixels const& reference) -> void {
  ASSERT_EQ(reference.size(), scaled.size());
  auto error = client.MaxError(reference);
  testing::Test::RecordProperty("scaled_maximum_channel_error", error);
  EXPECT_LE(error, 8u);
}
class AvcSession : public GraphicsBackend {
protected:
  auto GivenAutoFrame(bool avc = true) -> void {
    ASSERT_NO_FATAL_FAILURE(GivenGraphics(Codec::Auto, 320, 200, avc));
    PresentFrame(Pixels(320uz * 200, 0x55aaff));
  }
  auto PresentFrame(Pixels const& pixels, std::uint32_t width = 320, std::uint32_t height = 200) -> void {
    Extent const size{ .width = width, .height = height };
    Frame(pixels, size, Whole(size));
  }
  auto Open(Codec codec = Codec::Avc420, std::uint32_t width = 320, std::uint32_t height = 200) -> void {
    OpenGraphics("avc", width, height, codec);
  }
  auto ClientSession() -> Client& {
    return *graphics_client;
  }
  auto ObserverSession() -> GraphicsObserver& {
    return *graphics_observer;
  }
  auto ThenProgressiveOnly() -> void {
    EXPECT_TRUE(ObserverSession().Observed().avc_nals.empty());
    EXPECT_EQ(ObserverSession().Observed().progressive_headers, 1u);
  }
  auto GivenGraphics(Codec codec = Codec::Avc420, std::uint32_t width = 320, std::uint32_t height = 200,
                     bool avc = true) -> void {
    ASSERT_NO_FATAL_FAILURE(Open(codec, width, height));
    graphics_client = std::make_unique<Client>(backend.Port(), true);
    graphics_client->EnableGraphics({ .h264 = avc });
    if (!avc) SettingsOf(*graphics_client).Set(BoolKey::GfxH264, false);
    graphics_observer = std::make_unique<GraphicsObserver>(*graphics_client);
    ConnectGraphics(*graphics_client);
  }
  auto ThenReported(Client& client, Codec codec) -> void {
    bool reported = false;
    ASSERT_TRUE(client.Until([&] {
      auto const reports = [codec](auto const& event) { return event.codec == codec; };
      reported |= std::ranges::any_of(backend.Poll(), [&](auto const& event) {
        return Where<Connected>(reports)(event) || Where<CodecChanged>(reports)(event);
      });
      return reported;
    }));
  }
  static auto ReadScaledPixels(Client& client, Pixels& scaled) -> void {
    ASSERT_EQ(client.DesktopSize(), (Extent{ .width = 321, .height = 214 }));
    scaled = DecodedPixels(client) | std::ranges::to<Pixels>();
  }

  auto Frame(Pixels const& pixels, Extent size, Rect damage) -> void {
    auto& client     = ClientSession();
    auto& observer   = ObserverSession();
    auto  before     { observer.Observed().frames.size()   };
    auto  avc_before { observer.Observed().avc_nals.size() };
    backend.Present(pixels, size.width, size.height, damage);
    ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() > before; })) << logs.Text(true);
    auto reference = observer.Observed().avc_nals.size() > avc_before ? Yuv420Reference(pixels, size) : pixels;
    auto error     = client.MaxError(reference);
    RecordProperty("maximum_channel_error_" + std::to_string(observer.Observed().frames.size()), error);
    EXPECT_LE(error, 8u) << logs.Text(true);
    ASSERT_TRUE(client.Until([&] { return backend.WaitFrame(std::chrono::milliseconds{ 0 }); }));
  }
  auto TearDown() -> void override {
    graphics_observer.reset();
    graphics_client.reset();
    GraphicsBackend::TearDown();
  }

  auto ThenCodecChanged() -> void {
    bool changed = false;
    ASSERT_TRUE(ClientSession().Until([&] {
      changed |= std::ranges::any_of(
          backend.Poll(), Where<CodecChanged>([](auto const& event) { return event.codec == Codec::Progressive; }));
      return changed;
    }));
  }
  std::unique_ptr<Client>           graphics_client;
  std::unique_ptr<GraphicsObserver> graphics_observer;
};
class AvcGraphics : public AvcSession {
protected:
  auto GivenSmallSurface(Codec codec, Pixels const& pixels) -> void {
    ASSERT_NO_FATAL_FAILURE(GivenGraphics(codec, 32, 32));
    ASSERT_NO_FATAL_FAILURE(PresentFrame(pixels, 32, 32));
    ThenProgressiveOnly();
  }
  auto WhenProgressiveSwitchesToAvc(Pixels const& pixels) -> void {
    auto commands = ObserverSession().Observed().commands;
    (*backend).Presentation().SetCodec(Codec::Avc420);
    ASSERT_NO_FATAL_FAILURE(Frame(pixels, { .width = 320, .height = 200 }, { .x = 18, .y = 20, .w = 8, .h = 6 }));
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
  auto WhenSmallAvcRequested(Pixels const& pixels) -> void {
    while (!backend.Poll().empty()) {
    }
    (*backend).Presentation().SetCodec(Codec::Avc420);
    ASSERT_NO_FATAL_FAILURE(PresentFrame(pixels, 32, 32));
    ASSERT_NO_FATAL_FAILURE(ThenProgressiveOnly());
    EXPECT_EQ(logs.Count(LogLevel::Info, "surface below NVENC minimum"), 1u);
    EXPECT_EQ(logs.Count(LogLevel::Info, "falls back"), 1u);
    ThenCodecChanged();
  }
  auto ThenAvcResizes(Pixels& pixels) -> void {
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
  auto ThenScaledAvc(Client& client, GraphicsObserver& observer, Pixels const& pixels, Pixels const& scaled, Rect full)
      -> void {
    auto reference = Yuv420Reference(scaled, { .width = 321, .height = 214 });
    ASSERT_TRUE(client.Until([&] { return backend.WaitFrame(std::chrono::milliseconds{ 0 }); }));
    (*backend).Presentation().SetCodec(Codec::Avc420);
    auto avc_before = observer.Observed().avc_nals.size();
    backend.Present(pixels, 320, 200, full);
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
  auto ScaledPattern(Client& client, GraphicsObserver& observer, Pixels const& pixels) -> void {
    (*backend).Presentation().SetCodec(Codec::Raw);
    (*backend).Presentation().SetAspect(AspectRatio{ .numerator = 3, .denominator = 2 });
    auto       before{ observer.Observed().frames.size()  };
    Rect const full  { .x = 0, .y = 0, .w = 320, .h = 200 };
    backend.Present(pixels, 320, 200, full);
    ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() > before; }));
    Pixels scaled(321uz * 214);
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
    if (!Encoder::Available()) GTEST_SKIP() << Encoder::UnavailableReason();
  }
};

TEST_F(AvcAvailable, AutoWithAvcStartsWithIdr) {
  ASSERT_NO_FATAL_FAILURE(GivenAutoFrame());
  ASSERT_NO_FATAL_FAILURE(ThenReported(ClientSession(), Codec::Avc420));
  ASSERT_EQ(ObserverSession().Observed().avc_nals.size(), 1u);
  EXPECT_TRUE(ObserverSession().Observed().avc_nals.front() & (1u << 5));
  EXPECT_EQ(ObserverSession().Observed().progressive_headers, 0u);
  EXPECT_EQ(logs.Count(LogLevel::Info, "falls back"), 0u);
}
TEST_F(AvcGraphics, AutoWithoutAvcUsesProgressiveSilently) {
  ASSERT_NO_FATAL_FAILURE(GivenAutoFrame(false));
  ASSERT_NO_FATAL_FAILURE(ThenReported(ClientSession(), Codec::Progressive));
  ASSERT_NO_FATAL_FAILURE(ThenProgressiveOnly());
  EXPECT_EQ(logs.Count(LogLevel::Info, "falls back"), 0u);
}
TEST_F(AvcAvailable, DecodesPFrameAndResize) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphics(Codec::Avc420));
  Pixels                                 pixels(320uz * 200);
  constexpr std::array<std::uint32_t, 4> colors{ 0xff0000, 0x00ff00, 0x0000ff, 0x55aaff };
  std::ranges::transform(std::views::iota(0uz, pixels.size()), pixels.begin(),
                         [&](std::size_t i) { return colors[(i % 320) / 80]; });
  ASSERT_NO_FATAL_FAILURE(PresentFrame(pixels));
  Rect const damage{ .x = 18, .y = 20, .w = 8, .h = 6 };
  FillArea(pixels, 320, damage, 0x55aaffu);
  ASSERT_NO_FATAL_FAILURE(Frame(pixels, { .width = 320, .height = 200 }, damage));
  ASSERT_NO_FATAL_FAILURE(ThenPFrameDamage());
  ThenAvcResizes(pixels);
}
TEST_F(AvcGraphics, ClientWithoutAvcFallsBackAndReportsChange) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphics(Codec::Avc420, 320, 200, false));
  Pixels const pixels(320uz * 200, 0x55aaff);
  ASSERT_NO_FATAL_FAILURE(PresentFrame(pixels));
  ASSERT_NO_FATAL_FAILURE(ThenCodecChanged());
  EXPECT_EQ(ObserverSession().Observed().progressive_headers, 1u);
  EXPECT_EQ(logs.Count(LogLevel::Info, "AVC420 falls back"), 1u);
}
TEST_F(AvcAvailable, SmallSurfaceFallsBack) {
  Pixels const pixels(32uz * 32, 0x55aaff);
  ASSERT_NO_FATAL_FAILURE(GivenSmallSurface(Codec::Avc420, pixels));
  EXPECT_EQ(logs.Count(LogLevel::Info, "surface below NVENC minimum"), 1u);
}
TEST_F(AvcAvailable, AutoSmallSurfaceLogsFallbackWhenAvcRequested) {
  Pixels const pixels(32uz * 32, 0x55aaff);
  ASSERT_NO_FATAL_FAILURE(GivenSmallSurface(Codec::Auto, pixels));
  EXPECT_FALSE(logs.Contains("falls back"));
  EXPECT_FALSE(logs.Contains("surface below NVENC minimum"));
  WhenSmallAvcRequested(pixels);
}
TEST_F(AvcAvailable, ProgressiveConnectionSwitchesToAvcWithIdr) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphics(Codec::Progressive));
  Pixels const pixels(320uz * 200, 0x335577);
  ASSERT_NO_FATAL_FAILURE(PresentFrame(pixels));
  EXPECT_EQ(ObserverSession().Observed().progressive_headers, 1u);
  EXPECT_TRUE(ObserverSession().Observed().avc_nals.empty());
  WhenProgressiveSwitchesToAvc(pixels);
}
TEST_F(AvcAvailable, CodecSwitchRestoresFullSurfaceAndIdr) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphics(Codec::Avc420));
  Pixels pixels(320uz * 200, 0xff0000);
  ASSERT_NO_FATAL_FAILURE(PresentFrame(pixels));
  (*backend).Presentation().SetCodec(Codec::Raw);
  pixels.assign(pixels.size(), 0x335577);
  ASSERT_NO_FATAL_FAILURE(PresentFrame(pixels));
  (*backend).Presentation().SetCodec(Codec::Avc420);
  ASSERT_NO_FATAL_FAILURE(Frame(pixels, { .width = 320, .height = 200 }, { .x = 18, .y = 20, .w = 8, .h = 6 }));
  ThenFullSurfaceIdr();
}
TEST_F(AvcAvailable, KnownPatternColours) {
  ASSERT_NO_FATAL_FAILURE(GivenGraphics(Codec::Avc420));
  Pixels pixels(320uz * 200);
  MovingTilePattern(pixels, 320, 200, 0);
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
    auto refresh = IntraRefreshFor(value.fps);
    EXPECT_EQ(refresh.period, value.period);
    EXPECT_EQ(refresh.count, value.count);
  });
}

}
}
