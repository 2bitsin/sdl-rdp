#include "_detail/wake-event.hpp"

#include "_detail/contract.hpp"

#include <algorithm>
#include <chrono>
#include <gtest/gtest.h>
#include <ranges>
#include <thread>
#include <winpr/synch.h>

namespace {
void ConsumePublished(Backend::WakeEvent& wake, std::atomic<unsigned>& published, std::atomic<unsigned>& consumed) {
  std::ranges::for_each(std::views::iota(0, 4096), [&](int) {
    wake.Transition(Backend::WakeEvent::Phase::Idle);
    auto start = std::chrono::steady_clock::now();
    if (published.load() == consumed.load()) EXPECT_EQ(WaitForSingleObject(wake.get(), 10000), WAIT_OBJECT_0);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(10));
    consumed.store(published.load());
  });
}
namespace {
void ProducePending(Backend::WakeEvent& wake, std::atomic<unsigned>& published, std::atomic<unsigned> const& consumed,
                    std::stop_token const& stop) {
  while (!stop.stop_requested()) {
    published.store(consumed.load() + 1);
    wake.Transition(Backend::WakeEvent::Phase::Pending);
    std::this_thread::yield();
  }
}
}
TEST(WakeEvent, SignalledManualResetEvent) {
  Backend::EventHandle const event{ CreateEvent(nullptr, TRUE, FALSE, nullptr) };
  utilities::Expects(bool(event), "manual reset event exists");
  EXPECT_FALSE(Backend::Signalled(event.get()));
  ASSERT_TRUE(SetEvent(event.get()));
  EXPECT_TRUE(Backend::Signalled(event.get()));
  EXPECT_TRUE(Backend::Signalled(event.get()));
  ASSERT_TRUE(ResetEvent(event.get()));
  EXPECT_FALSE(Backend::Signalled(event.get()));
}

TEST(WakeEvent, ConcurrentPendingAndIdle) {
  using Phase = Backend::WakeEvent::Phase;
  Backend::WakeEvent    wake     { CreateEvent(nullptr, TRUE, FALSE, nullptr) };
  std::atomic<unsigned> published{ 0                                          };
  std::atomic<unsigned> consumed { 0                                          };
  ASSERT_TRUE(wake);
  std::jthread producer([&](std::stop_token const& stop) { ProducePending(wake, published, consumed, stop); });
  ConsumePublished(wake, published, consumed);
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
