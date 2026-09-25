#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/status.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/headless-client.test/graphics/backend.hpp>
#include <sdl-rdp/headless-client.test/graphics/cost.hpp>
#include <sdl-rdp/headless-client.test/graphics/observer.hpp>
#include <sdl-rdp/utilities/extent.hpp>
#include <sdl-rdp/video/gfx/protocol.hpp>

#include <gtest/gtest.h>
#include <oxbox/utilities/span.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <random>
#include <ranges>
#include <span>

namespace {
class GraphicsResize : public testing::Test {
protected:
  auto SetUp() -> void override {
    auto path = std::to_array("/tmp/sdlrdp-gfx-resize-XXXXXX");
    ASSERT_NE(mkdtemp(path.data()), nullptr);
    certificates = path.data();
    sdlrdp_config config{ "127.0.0.1", 0, certificates.c_str(), 640, 480, 0, Headless::Logs::Collect, &logs };
    config.codec = SDLRDP_CODEC_PROGRESSIVE;
    ASSERT_NO_FATAL_FAILURE(backend.Open(config));
  }
  auto TearDown() -> void override {
    backend.Close();
    if (!certificates.empty()) std::filesystem::remove_all(certificates);
  }
  auto PresentProgressivePixel(Headless::Client& client, Headless::GraphicsObserver& observer,
                               std::vector<std::uint32_t>& pixels, Backend::Extent size, std::size_t generations)
      -> void {
    auto              frames = observer.Observed().frames.size();
    sdlrdp_rect const damage = { .x = 0, .y = 0, .w = 1, .h = 1 };
    pixels.front() ^= 0x222222;
    ASSERT_EQ(backend.Present(pixels, size.width, size.height, damage), 0);
    ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() > frames; })) << logs.Text(true);
    EXPECT_LE(client.MaxError(pixels), client.Tolerance());
    EXPECT_EQ(observer.Observed().progressive_headers, generations);
  }
  std::filesystem::path     certificates;
  Headless::Logs            logs;
  Headless::BackendInstance backend;
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
auto BilinearRow(std::vector<std::uint32_t> const& pixels, std::size_t y) -> std::vector<std::uint32_t> {
  auto const position = std::clamp(((static_cast<double>(y) + 0.5) * (200.0 / 240)) - 0.5, 0.0, 199.0);
  auto const first    = static_cast<std::size_t>(position);
  auto const second   = std::min(first + 1, 199uz);
  auto const weight   = static_cast<float>(position - static_cast<double>(first));
  auto const top      = std::span(pixels).subspan(first * 320, 320);
  auto const bottom   = std::span(pixels).subspan(second * 320, 320);
  return std::views::zip_transform([=](std::uint32_t a, std::uint32_t b) { return Blend(a, b, weight); }, top, bottom)
         | std::ranges::to<std::vector>();
}
auto ThenBilinearRow(rdpGdi const& gdi, std::vector<std::uint32_t> const& pixels, std::size_t y) -> void {
  auto const frame    = std::span(gdi.primary_buffer, std::size_t{ gdi.stride } * static_cast<std::size_t>(gdi.height));
  auto const row      = frame.subspan(y * gdi.stride, 320 * Backend::PixelBytes);
  auto const actual   = oxbox::utilities::SpanCast<std::uint32_t const>(row);
  auto const expected = BilinearRow(pixels, y);
  for (std::size_t x = 0; x < 320; ++x) ASSERT_EQ(actual[x] & 0xffffff, expected[x]) << x << ',' << y;
}
auto ThenBilinearPixels(rdpGdi const& gdi, std::vector<std::uint32_t> const& pixels) -> void {
  for (std::size_t y = 0; y < 240; ++y) {
    ASSERT_NO_FATAL_FAILURE(ThenBilinearRow(gdi, pixels, y));
  }
}
auto ThenProgressiveGeneration(Headless::GraphicsObserver const& observer, std::size_t generations, std::uint32_t w,
                               std::uint32_t h) -> void {
  EXPECT_EQ(observer.Observed().progressive_headers, generations);
  EXPECT_EQ(observer.Observed().deleted, generations - 1);
  ASSERT_EQ(observer.Observed().surfaces.size(), generations);
  EXPECT_EQ(observer.Observed().surfaces.back().width, w);
  EXPECT_EQ(observer.Observed().surfaces.back().height, h);
}
constexpr std::array ResizeSequence{ std::pair{ 640u, 480u }, std::pair{ 320u, 200u }, std::pair{ 640u, 480u } };
TEST_F(GraphicsResize, RawAspectMatchesBilinear) {
  ASSERT_EQ(sdlrdp_set_codec(backend.Handle(), SDLRDP_CODEC_RAW), 0);
  ASSERT_EQ(sdlrdp_set_aspect(backend.Handle(), { 4, 3 }), 0);
  Headless::Client client(sdlrdp_port(backend.Handle()), true, 320, 240);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(client.Connect());
  std::vector<std::uint32_t> pixels(320uz * 200);
  std::mt19937               random(17);           // NOLINT(cert-msc32-c, cert-msc51-cpp): Reproducible codec input.
  std::ranges::generate(pixels, [&] { return random() & 0x00ffffff; });
  sdlrdp_rect const full{ 0, 0, 320, 200 };
  ASSERT_EQ(backend.Present(pixels, 320, 200, full), 0);
  ASSERT_TRUE(client.Until([&] { return !observer.Observed().frames.empty(); })) << logs.Text(true);
  auto* gdi = client.Instance()->context->gdi;
  ASSERT_EQ(gdi->width, 320);
  ASSERT_EQ(gdi->height, 240);
  ASSERT_EQ(observer.Observed().frames.size(), 1u);
  ThenBilinearPixels(*gdi, pixels);
}
TEST_F(GraphicsResize, ProgressiveContextAndFullDamage) {
  Headless::Client client(sdlrdp_port(backend.Handle()), true, 640, 480);
  client.EnableGraphics();
  client.Tolerance(24);
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(client.Connect());
  std::size_t generations = 0;
  for (auto [w, h] : ResizeSequence) {
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(w) * h, 0x335577 + (generations * 0x221100));
    SCOPED_TRACE(std::to_string(w) + "x" + std::to_string(h));
    sdlrdp_rect const damage{ 0, 0, int(w), int(h) };
    ASSERT_EQ(backend.Present(pixels, w, h, damage), 0);
    ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text(true);
    ++generations;
    ASSERT_NO_FATAL_FAILURE(ThenProgressiveGeneration(observer, generations, w, h));
    ASSERT_NO_FATAL_FAILURE(
        PresentProgressivePixel(client, observer, pixels, { .width = w, .height = h }, generations));
  }
}
}

namespace {
using Headless::GraphicsCost;

auto ApplyPlanarDamage(std::vector<std::uint32_t>& pixels, std::vector<std::uint32_t>& expected, sdlrdp_rect part)
    -> void {
  std::ranges::for_each(std::views::iota(part.y, part.y + part.h), [&](int row) {
    std::ranges::fill(std::span(pixels).subspan((row * 354) + part.x, part.w), 0x55aaffu);
  });
  expected       =  pixels;
  pixels.front() ^= 0x00ffffff;
  pixels.back()  ^= 0x00ffffff;
}
TEST_F(GraphicsCost, PlanarPartialMatchesFull) {
  ASSERT_NO_FATAL_FAILURE(Open(354, 226, SDLRDP_CODEC_PLANAR));
  Headless::Client client(sdlrdp_port(backend.Handle()), true, 354, 226);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  ASSERT_NO_FATAL_FAILURE(ConnectGraphics(client));
  std::vector<std::uint32_t> pixels(354uz * 226);
  Headless::MovingTilePattern(pixels, 354, 226, 0);
  sdlrdp_rect const full     { 0, 0, 354, 226   };
  sdlrdp_rect const part     { 17, 19, 177, 113 };
  auto              expected = pixels;
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
