#include <sdl-rdp/freerdp-facade/wake-event.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <gtest/gtest.h>
#include <winpr/synch.h>
#include <algorithm>
#include <chrono>
#include <ranges>
#include <thread>

namespace {
auto ConsumePublished(Backend::WakeEvent& wake, std::atomic<unsigned>& published, std::atomic<unsigned>& consumed)
    -> void {
  for (int iteration = 0; iteration < 4096; ++iteration) {
    wake.Transition(Backend::WakeEvent::Phase::Idle);
    if (published.load() == consumed.load()) ASSERT_EQ(WaitForSingleObject(wake.get(), 10000), WAIT_OBJECT_0);
    consumed.store(published.load());
  }
}
namespace {
auto ProducePending(Backend::WakeEvent& wake, std::atomic<unsigned>& published, std::atomic<unsigned> const& consumed,
                    std::stop_token const& stop) -> void {
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
