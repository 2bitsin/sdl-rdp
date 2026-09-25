#include <sdl-rdp/configuration/refresh.hpp>
#include <gtest/gtest.h>
using namespace std::chrono_literals;
namespace Backend {
TEST(RefreshEstimator, ClientLatencyIgnoresPresentSpacing) {
  Refresh value{ RefreshMode::Client };
  value.Restart();
  auto now = Refresh::Clock::now();
  for (int frame = 0; frame < 10; ++frame) value.Acknowledge(now += 100ms, 100ms);
  EXPECT_EQ(value.Rate(), 10u);
  for (int frame = 0; frame < 5; ++frame) value.Acknowledge(now += 1s, 1ms);
  EXPECT_EQ(value.Rate(), 60u);
}
TEST(RefreshEstimator, AverageHeldAcknowledgementRespectsFloor) {
  Refresh value{ RefreshMode::Average };
  value.Restart();
  auto now = Refresh::Clock::now();
  value.Acknowledge(now, 1ms);
  value.Acknowledge(now += 600ms, 600ms);
  EXPECT_EQ(value.Rate(), 10u);
  for (int frame = 0; frame < 20; ++frame) {
    value.Acknowledge(now += 100ms, 1ms);
    EXPECT_GE(value.Rate(), 10u);
  }
}
TEST(RefreshEstimator, SenderUsesQueueAndSegments) {
  Refresh value{ RefreshMode::Sender };
  value.Restart();
  WireSample wire{
    .available = true, .outq = 10001, .notsent = 500, .unacked = 10, .rtt = 100, .mss = 1000, .delivery_rate = 100000
  };
  for (int frame = 0; frame < 10; ++frame) value.Written(wire, 10000);
  EXPECT_EQ(value.Rate(), 10u);
  wire.outq    = 0;
  wire.unacked = 0;
  for (int frame = 0; frame < 5; ++frame) value.Written(wire, 10000);
  EXPECT_EQ(value.Rate(), 60u);
  wire.unacked = 11;
  value.Written(wire, 10000);
  EXPECT_EQ(value.Rate(), 50u);
}
namespace {
auto ThenBlockedIntervals(Refresh& value, Refresh::Clock::time_point now) -> void {
  value.Blocked(now);
  EXPECT_EQ(value.Rate(), 50u);
  value.Blocked(now + 19ms);
  EXPECT_EQ(value.Rate(), 50u);
  value.Blocked(now + 20ms);
  EXPECT_EQ(value.Rate(), 40u);
  value.Blocked(now + 44ms);
  EXPECT_EQ(value.Rate(), 40u);
  value.Blocked(now + 45ms);
  EXPECT_EQ(value.Rate(), 30u);
}
}
TEST(RefreshEstimator, BlockedSenderStepsOncePerCurrentInterval) {
  Refresh value{ RefreshMode::Sender };
  value.Restart();
  auto now = Refresh::Clock::now();
  ThenBlockedIntervals(value, now);
  for (auto elapsed = 50ms; elapsed <= 1s; elapsed += 5ms) value.Blocked(now + elapsed);
  EXPECT_EQ(value.Rate(), 10u);
  for (int frame = 0; frame < 5; ++frame) value.Written({ .available = true }, 10000);
  EXPECT_EQ(value.Rate(), 60u);
  value.Restart();
  value.Blocked(now + 1s);
  EXPECT_EQ(value.Rate(), 50u);
}
TEST(RefreshEstimator, BlockedTransportDoesNotChangeOtherModes) {
  for (auto mode : { RefreshMode::Fixed, RefreshMode::Client, RefreshMode::Average }) {
    Refresh value{ mode };
    value.Restart();
    auto now = Refresh::Clock::now();
    for (auto elapsed = 0ms; elapsed <= 1s; elapsed += 5ms) value.Blocked(now + elapsed);
    EXPECT_EQ(value.Rate(), 60u);
  }
}
namespace {
auto ThenOnlyEmptyQueueRecovers(Refresh& value) -> void {
  value.Blocked(Refresh::Clock::now());
  value.Written({ .available = true, .outq = 36 }, 10000);
  value.Drained({ .available = true, .outq = 36 });
  EXPECT_EQ(value.Rate(), 50u);
  value.Drained({ });
  EXPECT_EQ(value.Rate(), 50u);
  value.Drained({ .available = true });
  EXPECT_EQ(value.Rate(), 60u);
}
auto ThenRecoversOncePerCompletedFrame(Refresh& value) -> void {
  value.Step(Direction::Down);
  value.Drained({ .available = true });
  EXPECT_EQ(value.Rate(), 50u);
  value.Written({ .available = true }, 10000);
  value.Step(Direction::Down);
  value.Drained({ .available = true });
  EXPECT_EQ(value.Rate(), 50u);
}
auto ThenRestartForgetsQueuedFrame(Refresh& value) -> void {
  value.Written({ .available = true, .outq = 36 }, 10000);
  value.Restart();
  value.Step(Direction::Down);
  value.Drained({ .available = true });
  EXPECT_EQ(value.Rate(), 50u);
}
}
TEST(RefreshEstimator, EmptyQueueRecoversOnlyOncePerCompletedFrame) {
  Refresh value{ RefreshMode::Sender };
  value.Restart();
  ThenOnlyEmptyQueueRecovers(value);
  ThenRecoversOncePerCompletedFrame(value);
  ThenRestartForgetsQueuedFrame(value);
}
class RefreshReset : public testing::TestWithParam<RefreshMode> { };
TEST_P(RefreshReset, RestartDropsHistory) {
  Refresh value{ GetParam() };
  value.Restart();
  auto now = Refresh::Clock::now();
  for (int frame = 0; frame < 10; ++frame) {
    value.Acknowledge(now += 600ms, 100ms);
    value.Written({ .available     = true,
                    .outq          = 20000,
                    .notsent       = 10000,
                    .unacked       = 20,
                    .rtt           = 100,
                    .mss           = 1000,
                    .delivery_rate = 1000 },
                  10000);
  }
  ASSERT_EQ(value.Rate(), 10u);
  value.Restart();
  EXPECT_EQ(value.Rate(), 60u);
  value.Acknowledge(now + 5s, 1ms);
  EXPECT_EQ(value.Rate(), 60u);
}
INSTANTIATE_TEST_SUITE_P(Adaptive, RefreshReset,
                         testing::Values(RefreshMode::Client, RefreshMode::Average, RefreshMode::Sender));
}
