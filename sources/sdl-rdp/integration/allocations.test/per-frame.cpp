#include "counting-heap.hpp"
#include <sdl-rdp/configuration/codec.hpp>
#include <sdl-rdp/headless-client.test/backend/await-acknowledged.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/headless-client.test/graphics/backend.hpp>
#include <sdl-rdp/headless-client.test/graphics/observer.hpp>
#include <sdl-rdp/utilities/rect.hpp>
#include <sdl-rdp/video/avc/encoder.hpp>

#include <gtest/gtest.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace sdl_rdp::integration::allocations_test::detail::per_frame {
using sdl_rdp::configuration::Codec;
using sdl_rdp::headless_client_test::backend::AwaitAllAcknowledged;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::frame::MovingTilePattern;
using sdl_rdp::headless_client_test::graphics::GraphicsBackend;
using sdl_rdp::headless_client_test::graphics::GraphicsObserver;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Rect;
using sdl_rdp::video::avc::Encoder;
namespace {
constexpr std::uint32_t Width          = 640;
constexpr std::uint32_t Height         = 480;
constexpr std::size_t   WarmFrames     = 10;
constexpr std::size_t   MeasuredFrames = 100;

struct Scenario {
  std::string_view name;
  Codec            codec;
  bool             partial;
  Tally            ceiling;
};
// Ceilings: the highest of the 2026-09-24 debug-build runs on an RTX 3090 box, rounded up; they only go down.
constexpr std::array Scenarios{
  Scenario{ .name = "planar_full", .codec = Codec::Planar, .partial = false, .ceiling = { .news = 3, .heap = 5305 }  },
  Scenario{ .name = "planar_partial", .codec = Codec::Planar, .partial = true, .ceiling = { .news = 3, .heap = 377 } },
  Scenario{ .name    = "progressive_full",
            .codec   = Codec::Progressive,
            .partial = false,
            .ceiling = { .news = 3, .heap = 355 } },
  Scenario{ .name    = "progressive_partial",
            .codec   = Codec::Progressive,
            .partial = true,
            .ceiling = { .news = 3, .heap = 115 } },
  Scenario{ .name = "raw_full", .codec = Codec::Raw, .partial = false, .ceiling = { .news = 3, .heap = 5420 }     },
  Scenario{ .name = "avc420_full", .codec = Codec::Avc420, .partial = false, .ceiling = { .news = 3, .heap = 43 } },
};

auto PerFrame(std::pair<Tally, Tally> const& counted, std::uint64_t Tally::* member) -> double {
  Expects(counted.second.*member >= counted.first.*member, "allocation tallies only grow");
  return static_cast<double>(counted.second.*member - counted.first.*member) / MeasuredFrames;
}
auto Damage(bool partial) -> Rect {
  auto const width = static_cast<int>(Width);
  return partial ? Rect{ .x = 0, .y = 40, .w = width, .h = 32 }
                 : Rect{ .x = 0, .y = 0, .w = width, .h = static_cast<int>(Height) };
}
auto Report(Scenario const& scenario, std::pair<Tally, Tally> const& counted) -> void {
  auto const news = PerFrame(counted, &Tally::news);
  auto const heap = PerFrame(counted, &Tally::heap);
  ::testing::Test::RecordProperty(std::string(scenario.name) + "_new_per_frame", std::to_string(news));
  ::testing::Test::RecordProperty(std::string(scenario.name) + "_malloc_per_frame", std::to_string(heap));
  EXPECT_LE(news, static_cast<double>(scenario.ceiling.news)) << scenario.name;
  EXPECT_LE(heap, static_cast<double>(scenario.ceiling.heap)) << scenario.name;
}

class Allocations : public GraphicsBackend, public testing::WithParamInterface<Scenario> {
protected:
  auto Connect(Scenario const& scenario) -> void {
    _client = std::make_unique<Client>(backend.Port(), true, Width, Height);
    _client->EnableGraphics({ .h264 = scenario.codec == Codec::Avc420 });
    _observer = std::make_unique<GraphicsObserver>(*_client);
    ConnectGraphics(*_client);
  }
  auto Present(Scenario const& scenario, std::size_t first, std::size_t last) -> void {
    for (auto frame = first; frame < last; ++frame)
      ASSERT_NO_FATAL_FAILURE(PresentOne(Damage(scenario.partial), frame));
  }

private:
  auto PresentOne(Rect const& area, std::size_t frame) -> void {
    MovingTilePattern(_pixels, Width, Height, frame);
    auto const received = _observer->Observed().frames.size();
    backend.Present(_pixels, Width, Height, area);
    Uncounted const waiting;
    ASSERT_TRUE(_client->Until([&] { return _observer->Observed().frames.size() > received; })) << logs.Text(true);
    ASSERT_NO_FATAL_FAILURE(AwaitAllAcknowledged(*_client, backend, logs));
  }
  Pixels                            _pixels   = Pixels(std::size_t{ Width } * Height);
  std::unique_ptr<Client>           _client;
  std::unique_ptr<GraphicsObserver> _observer;
};

TEST_P(Allocations, PerPresentedFrame) {
  auto const& scenario = GetParam();
  if (scenario.codec == Codec::Avc420 && !Encoder::Available()) GTEST_SKIP() << Encoder::UnavailableReason();
  ASSERT_NO_FATAL_FAILURE(OpenGraphics("allocations", Width, Height, scenario.codec));
  ASSERT_NO_FATAL_FAILURE(Connect(scenario));
  ASSERT_NO_FATAL_FAILURE(Present(scenario, 0, WarmFrames));
  auto const before = CountingHeap::Shared().Current();
  ASSERT_NO_FATAL_FAILURE(Present(scenario, WarmFrames, WarmFrames + MeasuredFrames));
  Report(scenario, { before, CountingHeap::Shared().Current() });
}
INSTANTIATE_TEST_SUITE_P(Codecs, Allocations, testing::ValuesIn(Scenarios),
                         [](auto const& info) { return std::string(info.param.name); });
}
}
