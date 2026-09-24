#include "_detail/avc-encoder.hpp"
#include "_detail/graphics-observer.hpp"
#include "_detail/test-graphics-backend.hpp"
#include "_detail/test-pattern.hpp"
#include "allocations.test/counting-heap.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using HeapCount::CountingHeap;
using HeapCount::Tally;
constexpr std::uint32_t Width          = 640;
constexpr std::uint32_t Height         = 480;
constexpr std::size_t   WarmFrames     = 10;
constexpr std::size_t   MeasuredFrames = 100;

struct Scenario {
  std::string_view name;
  sdlrdp_codec     codec;
  bool             partial;
  Tally            ceiling;
};
// Ceilings: the highest of the 2026-09-24 debug-build runs on an RTX 3090 box, rounded up; they only go down.
constexpr std::array Scenarios{
  Scenario{ .name    = "planar_full", .codec = SDLRDP_CODEC_PLANAR, .partial = false,
            .ceiling = { .news = 3, .heap = 5305 } },
  Scenario{ .name    = "planar_partial", .codec = SDLRDP_CODEC_PLANAR, .partial = true,
            .ceiling = { .news = 3, .heap = 377 } },
  Scenario{ .name    = "progressive_full", .codec = SDLRDP_CODEC_PROGRESSIVE, .partial = false,
            .ceiling = { .news = 3, .heap = 355 } },
  Scenario{ .name    = "progressive_partial", .codec = SDLRDP_CODEC_PROGRESSIVE, .partial = true,
            .ceiling = { .news = 3, .heap = 115 } },
  Scenario{ .name    = "raw_full", .codec = SDLRDP_CODEC_RAW, .partial = false,
            .ceiling = { .news = 3, .heap = 5420 } },
  Scenario{ .name    = "avc420_full", .codec = SDLRDP_CODEC_AVC420, .partial = false,
            .ceiling = { .news = 3, .heap = 43 } },
};

auto PerFrame(std::pair<Tally, Tally> const& counted, std::uint64_t Tally::* member) -> double {
  utilities::Expects(counted.second.*member >= counted.first.*member, "allocation tallies only grow");
  return static_cast<double>(counted.second.*member - counted.first.*member) / MeasuredFrames;
}
auto Damage(bool partial) -> sdlrdp_rect {
  auto const width = static_cast<int>(Width);
  return partial ? sdlrdp_rect{ 0, 40, width, 32 } : sdlrdp_rect{ 0, 0, width, static_cast<int>(Height) };
}
auto Report(Scenario const& scenario, std::pair<Tally, Tally> const& counted) -> void {
  auto const news = PerFrame(counted, &Tally::news);
  auto const heap = PerFrame(counted, &Tally::heap);
  ::testing::Test::RecordProperty(std::string(scenario.name) + "_new_per_frame", std::to_string(news));
  ::testing::Test::RecordProperty(std::string(scenario.name) + "_malloc_per_frame", std::to_string(heap));
  EXPECT_LE(news, static_cast<double>(scenario.ceiling.news)) << scenario.name;
  EXPECT_LE(heap, static_cast<double>(scenario.ceiling.heap)) << scenario.name;
}

class Allocations
  : public Headless::GraphicsBackend
  , public testing::WithParamInterface<Scenario> {
protected:
  auto Serve(Scenario const& scenario) -> std::pair<Tally, Tally> {
    Headless::Client client(sdlrdp_port(backend.get()), true, Width, Height);
    client.EnableGraphics(scenario.codec == SDLRDP_CODEC_AVC420);
    Headless::GraphicsObserver observer(client);
    ConnectGraphics(client);
    Present(client, observer, scenario, 0, WarmFrames);
    auto const before = CountingHeap::Shared().Current();
    Present(client, observer, scenario, WarmFrames, WarmFrames + MeasuredFrames);
    return { before, CountingHeap::Shared().Current() };
  }

private:
  auto Present(Headless::Client& client, Headless::GraphicsObserver& observer, Scenario const& scenario,
               std::size_t first, std::size_t last) -> void {
    auto const area = Damage(scenario.partial);
    for (auto frame = first; frame < last && !HasFatalFailure(); ++frame) {
      Headless::MovingTilePattern(pixels, Width, Height, frame);
      auto const received = observer.Observed().frames.size();
      ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), Width * 4, Width, Height, &area, 1), 0);
      HeapCount::Uncounted const client_side;
      ASSERT_TRUE(client.Until([&] { return observer.Observed().frames.size() > received; })) << logs.Text(true);
    }
  }
  std::vector<std::uint32_t> pixels = std::vector<std::uint32_t>(std::size_t{ Width } * Height);
};

TEST_P(Allocations, PerPresentedFrame) {
  auto const& scenario = GetParam();
  if (scenario.codec == SDLRDP_CODEC_AVC420 && !Backend::Avc::Encoder::Available())
    GTEST_SKIP() << Backend::Avc::Encoder::UnavailableReason();
  auto pattern = std::to_array("/tmp/sdlrdp-allocations-XXXXXX");
  OpenGraphics(pattern.data(), Width, Height, scenario.codec);
  ASSERT_FALSE(HasFatalFailure());
  auto const counted = Serve(scenario);
  ASSERT_FALSE(HasFatalFailure());
  Report(scenario, counted);
}
INSTANTIATE_TEST_SUITE_P(Codecs, Allocations, testing::ValuesIn(Scenarios),
                         [](auto const& info) { return std::string(info.param.name); });
}
