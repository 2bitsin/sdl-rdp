#include <sdl-rdp/configuration/codec.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/headless-client.test/backend/config.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/status.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/headless-client.test/graphics/backend.hpp>
#include <sdl-rdp/headless-client.test/graphics/cost.hpp>
#include <sdl-rdp/headless-client.test/graphics/observer.hpp>
#include <sdl-rdp/utilities/aspect-ratio.hpp>
#include <sdl-rdp/utilities/extent.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/rect.hpp>
#include <sdl-rdp/video/gfx/protocol.hpp>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <oxbox/utilities/span.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <ranges>
#include <span>

namespace sdl_rdp::integration::video_test::detail::gfx_resize {
using sdl_rdp::configuration::Codec;
using sdl_rdp::headless_client_test::backend::BackendInstance;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::headless_client_test::backend::LoopbackConfig;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::frame::MovingTilePattern;
using sdl_rdp::headless_client_test::graphics::GraphicsObserver;
using sdl_rdp::utilities::AspectRatio;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::PixelBytes;
using sdl_rdp::utilities::Rect;
namespace {
class GraphicsResize : public testing::Test {
protected:
  auto SetUp() -> void override {
    auto config = LoopbackConfig(certificates.Path(), { .width = 640, .height = 480 });
    config.codec = Codec::Progressive;
    ASSERT_NO_FATAL_FAILURE(backend.Open(config, logs));
  }
  auto TearDown() -> void override {
    backend.Close();
  }
  auto PresentProgressivePixel(Client& client, GraphicsObserver& observer, Pixels& pixels, Extent size,
                               std::size_t generations) -> void {
    auto       frames = observer.Observed().frames.size();
    Rect const damage = { .x = 0, .y = 0, .w = 1, .h = 1 };
    pixels.front() ^= 0x222222;
    backend.Present(pixels, size.width, size.height, damage);
    ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() > frames; })) << logs.Text(true);
    EXPECT_LE(client.MaxError(pixels), client.Tolerance());
    EXPECT_EQ(observer.Observed().progressive_headers, generations);
  }
  oxbox::platform::ScratchArea certificates{ "gfx-resize", "sdl-rdp" };
  Logs                         logs;
  BackendInstance              backend;
};
auto Blend(std::uint32_t top, std::uint32_t bottom, float weight) -> std::uint32_t {
  std::uint32_t blended = 0;
  for (std::size_t c = 0; c < 3; ++c) {
    auto const a = static_cast<float>((top >> (c * 8)) & 255);
    auto const b = static_cast<float>((bottom >> (c * 8)) & 255);
    // NOLINTNEXTLINE(bugprone-incorrect-roundings): Nonnegative wire-channel rounding.
    blended |= std::uint32_t{ static_cast<std::uint8_t>(a + ((b - a) * weight) + 0.5f) } << (c * 8);
  }
  return blended;
}
auto BilinearRow(Pixels const& pixels, std::size_t y) -> Pixels {
  auto const position = std::clamp(((static_cast<double>(y) + 0.5) * (200.0 / 240)) - 0.5, 0.0, 199.0);
  auto const first    = static_cast<std::size_t>(position);
  auto const second   = std::min(first + 1, 199uz);
  auto const weight   = static_cast<float>(position - static_cast<double>(first));
  auto const top      = std::span(pixels).subspan(first * 320, 320);
  auto const bottom   = std::span(pixels).subspan(second * 320, 320);
  return std::views::zip_transform([=](std::uint32_t a, std::uint32_t b) { return Blend(a, b, weight); }, top, bottom)
         | std::ranges::to<std::vector>();
}
auto ThenBilinearRow(rdpGdi const& gdi, Pixels const& pixels, std::size_t y) -> void {
  auto const frame    = std::span(gdi.primary_buffer, std::size_t{ gdi.stride } * static_cast<std::size_t>(gdi.height));
  auto const row      = frame.subspan(y * gdi.stride, 320 * PixelBytes);
  auto const actual   = oxbox::utilities::SpanCast<std::uint32_t const>(row);
  auto const expected = BilinearRow(pixels, y);
  for (std::size_t x = 0; x < 320; ++x) ASSERT_EQ(actual[x] & 0xffffff, expected[x]) << x << ',' << y;
}
auto ThenBilinearPixels(rdpGdi const& gdi, Pixels const& pixels) -> void {
  for (std::size_t y = 0; y < 240; ++y) {
    ASSERT_NO_FATAL_FAILURE(ThenBilinearRow(gdi, pixels, y));
  }
}
auto ThenProgressiveGeneration(GraphicsObserver const& observer, std::size_t generations, std::uint32_t w,
                               std::uint32_t h) -> void {
  EXPECT_EQ(observer.Observed().progressive_headers, generations);
  EXPECT_EQ(observer.Observed().deleted, generations - 1);
  ASSERT_EQ(observer.Observed().surfaces.size(), generations);
  EXPECT_EQ(observer.Observed().surfaces.back().width, w);
  EXPECT_EQ(observer.Observed().surfaces.back().height, h);
}
constexpr std::array ResizeSequence{ std::pair{ 640u, 480u }, std::pair{ 320u, 200u }, std::pair{ 640u, 480u } };
TEST_F(GraphicsResize, RawAspectMatchesBilinear) {
  (*backend).Presentation().SetCodec(Codec::Raw);
  (*backend).Presentation().SetAspect(AspectRatio{ .numerator = 4, .denominator = 3 });
  Client client(backend.Port(), true, 320, 240);
  client.EnableGraphics();
  GraphicsObserver observer(client);
  ASSERT_TRUE(client.Connect());
  Pixels       pixels(320uz * 200);
  std::mt19937 random(17);           // NOLINT(cert-msc32-c, cert-msc51-cpp): Reproducible codec input.
  std::ranges::generate(pixels, [&] { return random() & 0x00ffffff; });
  Rect const full{ .x = 0, .y = 0, .w = 320, .h = 200 };
  backend.Present(pixels, 320, 200, full);
  ASSERT_TRUE(client.Until([&] { return !observer.Observed().frames.empty(); })) << logs.Text(true);
  auto* gdi = client.Instance()->context->gdi;
  ASSERT_EQ(gdi->width, 320);
  ASSERT_EQ(gdi->height, 240);
  ASSERT_EQ(observer.Observed().frames.size(), 1u);
  ThenBilinearPixels(*gdi, pixels);
}
TEST_F(GraphicsResize, ProgressiveContextAndFullDamage) {
  Client client(backend.Port(), true, 640, 480);
  client.EnableGraphics();
  client.Tolerance(24);
  GraphicsObserver observer(client);
  ASSERT_TRUE(client.Connect());
  std::size_t generations = 0;
  for (auto [w, h] : ResizeSequence) {
    Pixels pixels(static_cast<std::size_t>(w) * h, 0x335577 + (generations * 0x221100));
    SCOPED_TRACE(std::to_string(w) + "x" + std::to_string(h));
    Rect const damage{ .x = 0, .y = 0, .w = Narrowed<int>(w), .h = Narrowed<int>(h) };
    backend.Present(pixels, w, h, damage);
    ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text(true);
    ++generations;
    ASSERT_NO_FATAL_FAILURE(ThenProgressiveGeneration(observer, generations, w, h));
    ASSERT_NO_FATAL_FAILURE(
        PresentProgressivePixel(client, observer, pixels, { .width = w, .height = h }, generations));
  }
}
using sdl_rdp::headless_client_test::graphics::GraphicsCost;

auto ApplyPlanarDamage(Pixels& pixels, Pixels& expected, Rect part) -> void {
  std::ranges::for_each(std::views::iota(part.y, part.y + part.h), [&](int row) {
    std::ranges::fill(std::span(pixels).subspan((row * 354) + part.x, part.w), 0x55aaffu);
  });
  expected       =  pixels;
  pixels.front() ^= 0x00ffffff;
  pixels.back()  ^= 0x00ffffff;
}
TEST_F(GraphicsCost, PlanarPartialMatchesFull) {
  ASSERT_NO_FATAL_FAILURE(Open(354, 226, Codec::Planar));
  Client client(backend.Port(), true, 354, 226);
  client.EnableGraphics();
  GraphicsObserver observer(client);
  ASSERT_NO_FATAL_FAILURE(ConnectGraphics(client));
  Pixels pixels(354uz * 226);
  MovingTilePattern(pixels, 354, 226, 0);
  Rect const full     { .x = 0, .y = 0, .w = 354, .h = 226   };
  Rect const part     { .x = 17, .y = 19, .w = 177, .h = 113 };
  auto       expected = pixels;
  ASSERT_NO_FATAL_FAILURE(PresentPlanar(client, observer, pixels, expected, full));
  ApplyPlanarDamage(pixels, expected, part);
  ASSERT_NO_FATAL_FAILURE(PresentPlanar(client, observer, pixels, expected, part));
  pixels = expected;
  auto*                     gdi     = client.Instance()->context->gdi;
  std::vector<std::uint8_t> partial(gdi->primary_buffer,
                                    gdi->primary_buffer + (std::size_t{ gdi->stride } * gdi->height));
  ASSERT_NO_FATAL_FAILURE(PresentPlanar(client, observer, pixels, expected, full));
  EXPECT_TRUE(std::ranges::equal(partial, std::span(gdi->primary_buffer, partial.size())));
}
}
}
