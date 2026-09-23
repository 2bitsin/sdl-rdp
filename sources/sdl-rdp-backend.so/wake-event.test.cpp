#include "_detail/wake-event.hpp"
#include "_detail/contract.hpp"
#include <gtest/gtest.h>
#include <winpr/synch.h>
#include <chrono>
#include <ranges>
#include <algorithm>
#include <thread>

namespace {
TEST(WakeEvent, SignalledManualResetEvent)
{
  Backend::EventHandle event{CreateEvent(nullptr, TRUE, FALSE, nullptr)};
  utilities::Expects(bool(event), "manual reset event exists");
  EXPECT_FALSE(Backend::Signalled(event.get()));
  ASSERT_TRUE(SetEvent(event.get()));
  EXPECT_TRUE(Backend::Signalled(event.get()));
  EXPECT_TRUE(Backend::Signalled(event.get()));
  ASSERT_TRUE(ResetEvent(event.get()));
  EXPECT_FALSE(Backend::Signalled(event.get()));
}

TEST(WakeEvent, ConcurrentPendingAndIdle)
{
  using Phase = Backend::WakeEvent::Phase;
  using Clock = std::chrono::steady_clock;
  Backend::WakeEvent    wake      { CreateEvent(nullptr, TRUE, FALSE, nullptr) };
  std::atomic<unsigned> published { 0                                          };
  std::atomic<unsigned> consumed  { 0                                          };
  ASSERT_TRUE(wake);
  std::jthread producer([&](std::stop_token stop) {
    while (!stop.stop_requested()) {
      published.store(consumed.load() + 1);
      wake.Transition(Phase::Pending);
      std::this_thread::yield();
    }
  });
  std::ranges::for_each(std::views::iota(0, 4096), [&](int) {
    wake.Transition(Phase::Idle);
    auto start = Clock::now();
    if (published.load() == consumed.load())
      EXPECT_EQ(WaitForSingleObject(wake.get(), 10000), WAIT_OBJECT_0);
    EXPECT_LT(Clock::now() - start, std::chrono::seconds(10));
    consumed.store(published.load());
  });
  producer.request_stop();
  producer.join();
  wake.Transition(Phase::Idle);
  EXPECT_FALSE(Backend::Signalled(wake.get()));
  // Reproduce an event set after a consumer observed Idle, before it stored Idle.
  ASSERT_TRUE(SetEvent(wake.get()));
  wake.Transition(Phase::Idle);
  EXPECT_FALSE(Backend::Signalled(wake.get()));
}
}
