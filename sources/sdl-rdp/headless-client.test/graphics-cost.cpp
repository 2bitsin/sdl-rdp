#include <sdl-rdp/headless-client.test/graphics-cost.hpp>
#include <sdl-rdp/headless-client.test/await-acknowledged.hpp>
#include <sdl-rdp/headless-client.test/backend-instance.hpp>

#include <sdl-rdp/headless-client.test/pattern.hpp>

#include <array>
#include <cstdint>
#include <regex>
#include <string>
#include <utility>

namespace Headless {
namespace {
auto MatchCostStatistics(std::string const& text, std::smatch& match, char const* expression) -> void {
  ASSERT_TRUE(std::regex_search(text, match, std::regex(expression))) << text;
}
auto RecordProgressiveCost(Logs& logs) -> void {
  auto        text  = logs.Text(true);
  std::smatch match;
  ASSERT_NO_FATAL_FAILURE(MatchCostStatistics(
      text, match,
      R"(Frames: 1 sent, 0 coalesced; encode ([0-9.]+) ms mean, ([0-9.]+) ms max; )"
      R"(acknowledgement ([0-9.]+) ms mean, ([0-9.]+) ms max, ([0-9]+) over 100 ms, [0-9]+ timed out\.)"));
  auto milliseconds = std::stod(match[1]);
  testing::Test::RecordProperty("encode_ms", milliseconds);
  testing::Test::RecordProperty("statistics", match.str());
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "Frames:"), 1u);
  EXPECT_EQ(match[1], match[2]);
  EXPECT_EQ(match[3], match[4]);
  EXPECT_GT(milliseconds, 0.0);
}
}
auto GraphicsCost::ThenProgressiveCost(Client& client, GraphicsObserver& observer) -> void {
  ASSERT_EQ(observer.Observed().frames.size(), 1u);
  client.Disconnect();
  backend.Close();
  RecordProgressiveCost(logs);
}
auto GraphicsCost::Open(std::uint32_t width, std::uint32_t height, sdlrdp_codec codec) -> void {
  auto pattern = std::to_array("/tmp/sdlrdp-cost-XXXXXX");
  OpenGraphics(pattern.data(), width, height, codec);
}
auto GraphicsCost::PresentMovingTiles(Client& client, std::size_t frames) -> void {
  std::vector<std::uint32_t> pixels(1920uz * 1080);
  sdlrdp_rect const          full  { 0, 0, 1920, 1080 };
  for (std::size_t frame = 0; frame < frames; ++frame) {
    MovingTilePattern(pixels, 1920, 1080, frame);
    ASSERT_EQ(backend.Present(pixels, 1920, 1080, full), 0);
    ASSERT_NO_FATAL_FAILURE(sdl_rdp::headless_client_test::AwaitAllAcknowledged(client, backend, logs));
  }
}
auto GraphicsCost::PresentPlanar(Client& client, GraphicsObserver& observer, std::vector<std::uint32_t> const& pixels,
                                 std::vector<std::uint32_t> const& expected, sdlrdp_rect area) -> void {
  auto count = observer.Observed().frames.size();
  EXPECT_EQ(backend.Present(pixels, 354, 226, area), 0);
  EXPECT_TRUE(client.Until([&] { return observer.Observed().frames.size() > count; }));
  EXPECT_EQ(client.MaxError(expected, &expected), 0u);
}
auto GraphicsCost::RecordAvcCost(Logs& logs) -> void {
  auto        text  = logs.Text(true);
  std::smatch match;
  ASSERT_NO_FATAL_FAILURE(
      MatchCostStatistics(text, match,
                          R"(Frames: 10 sent, 0 coalesced; encode ([0-9.]+) ms mean, ([0-9.]+) ms max )"
                          R"(\(convert ([0-9.]+), upload ([0-9.]+), nvenc ([0-9.]+)\); acknowledgement)"));
  for (auto [name, index] : { std::pair{ "encode_ms", 1 }, { "convert_ms", 3 }, { "upload_ms", 4 }, { "nvenc_ms", 5 } })
    testing::Test::RecordProperty(name, match[index].str());
  testing::Test::RecordProperty("statistics", match.str());
  // measured 2026-09-23 on an RTX 3090 at 1920x1080: 8.8 ms mean, 16.4 ms max after the row-copy dispatch (45.2 before)
  EXPECT_LT(std::stod(match[1]), 20.0);
}
}
