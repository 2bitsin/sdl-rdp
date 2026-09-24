#include "_detail/avc-encoder.hpp"
#include "_detail/extent.hpp"
#include "_detail/gfx-protocol.hpp"
#include "_detail/graphics-observer.hpp"
#include "_detail/test-graphics-backend.hpp"
#include "_detail/test-pattern.hpp"
#include "_detail/test-peer-status.hpp"

#include <gtest/gtest.h>
#include <oxbox/utilities/span.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <random>
#include <ranges>
#include <regex>
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
    sdlrdp_handle* handle = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
    backend.reset(handle);
  }
  auto TearDown() -> void override {
    backend.reset();
    if (!certificates.empty()) std::filesystem::remove_all(certificates);
  }
  auto PresentProgressivePixel(Headless::Client& client, Headless::GraphicsObserver& observer,
                               std::vector<std::uint32_t>& pixels, Backend::Extent size, std::size_t generations)
      -> void {
    auto              frames = observer.Observed().frames.size();
    sdlrdp_rect const damage = { .x = 0, .y = 0, .w = 1, .h = 1 };
    pixels.front() ^= 0x222222;
    auto const pitch = static_cast<int>(size.width * Backend::PixelBytes);
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), pitch, size.width, size.height, &damage, 1), 0);
    ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() > frames; })) << logs.Text(true);
    EXPECT_LE(client.MaxError(pixels), client.Tolerance());
    EXPECT_EQ(observer.Observed().progressive_headers, generations);
  }
  std::filesystem::path                                   certificates;
  Headless::Logs                                          logs;
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> backend     { nullptr, sdlrdp_close };
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
    ThenBilinearRow(gdi, pixels, y);
    if (::testing::Test::HasFatalFailure()) return;
  }
}
auto ThenProgressiveGeneration(Headless::GraphicsObserver const& observer, unsigned generations, unsigned w, unsigned h)
    -> void {
  EXPECT_EQ(observer.Observed().progressive_headers, generations);
  EXPECT_EQ(observer.Observed().deleted, generations - 1);
  ASSERT_EQ(observer.Observed().surfaces.size(), generations);
  EXPECT_EQ(observer.Observed().surfaces.back().width, w);
  EXPECT_EQ(observer.Observed().surfaces.back().height, h);
}
constexpr std::array ResizeSequence{ std::pair{ 640u, 480u }, std::pair{ 320u, 200u }, std::pair{ 640u, 480u } };
auto MatchCostStatistics(std::string const& text, std::smatch& match, char const* expression) -> void {
  ASSERT_TRUE(std::regex_search(text, match, std::regex(expression))) << text;
}
auto RecordProgressiveCost(Headless::Logs& logs) -> void {
  auto        text  = logs.Text(true);
  std::smatch match;
  MatchCostStatistics(
      text, match,
      R"(Frames: 1 sent, 0 coalesced; encode ([0-9.]+) ms mean, ([0-9.]+) ms max; )"
      R"(acknowledgement ([0-9.]+) ms mean, ([0-9.]+) ms max, ([0-9]+) over 100 ms, [0-9]+ timed out\.)");
  if (::testing::Test::HasFatalFailure()) return;
  auto milliseconds = std::stod(match[1]);
  testing::Test::RecordProperty("encode_ms", milliseconds);
  testing::Test::RecordProperty("statistics", match.str());
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "Frames:"), 1u);
  EXPECT_EQ(match[1], match[2]);
  EXPECT_EQ(match[3], match[4]);
  EXPECT_GT(milliseconds, 0.0);
}
TEST_F(GraphicsResize, RawAspectMatchesBilinear) {
  ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_RAW), 0);
  ASSERT_EQ(sdlrdp_set_aspect(backend.get(), { 4, 3 }), 0);
  Headless::Client client(sdlrdp_port(backend.get()), true, 320, 240);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  std::vector<UINT32> pixels(320uz * 200);
  std::mt19937        random(17);           // NOLINT(cert-msc32-c, cert-msc51-cpp): Reproducible codec input.
  std::ranges::generate(pixels, [&] { return random() & 0x00ffffff; });
  sdlrdp_rect const full{ 0, 0, 320, 200 };
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
  ASSERT_TRUE(client.Until([&] { return !observer.Observed().frames.empty(); })) << logs.Text(true);
  auto* gdi = client.Instance()->context->gdi;
  ASSERT_EQ(gdi->width, 320);
  ASSERT_EQ(gdi->height, 240);
  ASSERT_EQ(observer.Observed().frames.size(), 1u);
  ThenBilinearPixels(*gdi, pixels);
}
TEST_F(GraphicsResize, ProgressiveContextAndFullDamage) {
  Headless::Client client(sdlrdp_port(backend.get()), true, 640, 480);
  client.EnableGraphics();
  client.Tolerance(24);
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  unsigned generations = 0;
  for (auto [w, h] : ResizeSequence) {
    std::vector<UINT32> pixels(static_cast<std::size_t>(w) * h, 0x335577 + (generations * 0x221100));
    SCOPED_TRACE(std::to_string(w) + "x" + std::to_string(h));
    sdlrdp_rect const damage{ 0, 0, int(w), int(h) };
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), w * 4, w, h, &damage, 1), 0);
    ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text(true);
    ++generations;
    ThenProgressiveGeneration(observer, generations, w, h);
    if (::testing::Test::HasFatalFailure()) return;
    PresentProgressivePixel(client, observer, pixels, { .width = w, .height = h }, generations);
    if (::testing::Test::HasFatalFailure()) return;
  }
}
}

namespace {
class GraphicsCost : public Headless::GraphicsBackend {
protected:
  auto ThenProgressiveCost(Headless::Client& client, Headless::GraphicsObserver& observer) -> void {
    ASSERT_EQ(observer.Observed().frames.size(), 1u);
    freerdp_disconnect(client.Instance().get());
    backend.reset();
    RecordProgressiveCost(logs);
  }
  auto AwaitAcknowledgement(Headless::Client& client, uint64_t sequence) -> void {
    ASSERT_TRUE(client.Until([&] {
      auto const status = BackendGate::CurrentStatus(*backend);
      return status && status->acknowledged == sequence;
    })) << logs.Text(true);
  }

  auto Open(unsigned width = 1280, unsigned height = 800, sdlrdp_codec codec = SDLRDP_CODEC_PROGRESSIVE) -> void {
    auto pattern = std::to_array("/tmp/sdlrdp-cost-XXXXXX");
    OpenGraphics(pattern.data(), width, height, codec);
  }
  auto PresentMovingTiles(Headless::Client& client, std::uint32_t frames) -> void;
  auto PresentPlanar(Headless::Client& client, Headless::GraphicsObserver& observer,
                     std::vector<std::uint32_t> const& pixels, std::vector<std::uint32_t> const& expected,
                     sdlrdp_rect area) -> void;
};
auto GraphicsCost::PresentMovingTiles(Headless::Client& client, std::uint32_t frames) -> void {
  std::vector<std::uint32_t> pixels(1920uz * 1080);
  sdlrdp_rect const          full  { 0, 0, 1920, 1080 };
  for (std::uint32_t frame = 0; frame < frames; ++frame) {
    Headless::MovingTilePattern(pixels, 1920, 1080, frame);
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 7680, 1920, 1080, &full, 1), 0);
    AwaitAcknowledgement(client, frame + 1);
    if (::testing::Test::HasFatalFailure()) return;
  }
}
auto GraphicsCost::PresentPlanar(Headless::Client& client, Headless::GraphicsObserver& observer,
                                 std::vector<std::uint32_t> const& pixels, std::vector<std::uint32_t> const& expected,
                                 sdlrdp_rect area) -> void {
  auto count = observer.Observed().frames.size();
  EXPECT_EQ(sdlrdp_present(backend.get(), pixels.data(), 354 * 4, 354, 226, &area, 1), 0);
  EXPECT_TRUE(client.Until([&] { return observer.Observed().frames.size() > count; }));
  EXPECT_EQ(client.MaxError(expected, &expected), 0u);
}
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
auto RecordAvcCost(Headless::Logs& logs) -> void {
  auto        text  = logs.Text(true);
  std::smatch match;
  MatchCostStatistics(text, match,
                      R"(Frames: 10 sent, 0 coalesced; encode ([0-9.]+) ms mean, ([0-9.]+) ms max )"
                      R"(\(convert ([0-9.]+), upload ([0-9.]+), nvenc ([0-9.]+)\); acknowledgement)");
  if (::testing::Test::HasFatalFailure()) return;
  for (auto [name, index] : { std::pair{ "encode_ms", 1 }, { "convert_ms", 3 }, { "upload_ms", 4 }, { "nvenc_ms", 5 } })
    testing::Test::RecordProperty(name, match[index].str());
  testing::Test::RecordProperty("statistics", match.str());
  // measured 2026-09-23 on an RTX 3090 at 1920x1080: 8.8 ms mean, 16.4 ms max after the row-copy dispatch (45.2 before)
  EXPECT_LT(std::stod(match[1]), 20.0);
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

auto ApplyPlanarDamage(std::vector<UINT32>& pixels, std::vector<UINT32>& expected, sdlrdp_rect part) -> void {
  std::ranges::for_each(std::views::iota(part.y, part.y + part.h), [&](int row) {
    std::ranges::fill(std::span(pixels).subspan((row * 354) + part.x, part.w), 0x55aaffu);
  });
  expected       =  pixels;
  pixels.front() ^= 0x00ffffff;
  pixels.back()  ^= 0x00ffffff;
}
TEST_F(GraphicsCost, PlanarPartialMatchesFull) {
  Open(354, 226, SDLRDP_CODEC_PLANAR);
  if (::testing::Test::HasFatalFailure()) return;
  Headless::Client client(sdlrdp_port(backend.get()), true, 354, 226);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  ConnectGraphics(client);
  if (::testing::Test::HasFatalFailure()) return;
  std::vector<UINT32> pixels(354uz * 226);
  Headless::MovingTilePattern(pixels, 354, 226, 0);
  sdlrdp_rect const full     { 0, 0, 354, 226   };
  sdlrdp_rect const part     { 17, 19, 177, 113 };
  auto              expected = pixels;
  PresentPlanar(client, observer, pixels, expected, full);
  ApplyPlanarDamage(pixels, expected, part);
  PresentPlanar(client, observer, pixels, expected, part);
  pixels = expected;
  auto*             gdi     = client.Instance()->context->gdi;
  std::vector<BYTE> partial(gdi->primary_buffer, gdi->primary_buffer + (std::size_t(gdi->stride) * gdi->height));
  PresentPlanar(client, observer, pixels, expected, full);
  EXPECT_TRUE(std::ranges::equal(partial, std::span(gdi->primary_buffer, partial.size())));
}
}
