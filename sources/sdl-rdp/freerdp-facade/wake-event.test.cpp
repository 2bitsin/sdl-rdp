#include <sdl-rdp/freerdp-facade/wake-event.hpp>
#include <sdl-rdp/freerdp-facade/waitable.hpp>

#include <sdl-rdp/freerdp-facade/manual-reset-event.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <gtest/gtest.h>
#include <winpr/synch.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <ranges>
#include <thread>

namespace sdl_rdp::freerdp_facade::detail::wake_event {
namespace {
auto ConsumePublished(WakeEvent& wake, std::atomic<std::size_t>& published, std::atomic<std::size_t>& consumed)
    -> void {
  for (int iteration = 0; iteration < 4096; ++iteration) {
    wake.Transition(WakeEvent::Phase::Idle);
    if (published.load() == consumed.load()) ASSERT_EQ(WaitForSingleObject(wake.get(), 10000), WAIT_OBJECT_0);
    consumed.store(published.load());
  }
}
namespace {
auto ProducePending(WakeEvent& wake, std::atomic<std::size_t>& published, std::atomic<std::size_t> const& consumed,
                    std::stop_token const& stop) -> void {
  while (!stop.stop_requested()) {
    published.store(consumed.load() + 1);
    wake.Transition(WakeEvent::Phase::Pending);
    std::this_thread::yield();
  }
}
}
TEST(WakeEvent, ConcurrentPendingAndIdle) {
  using Phase = WakeEvent::Phase;
  WakeEvent                wake     { ManualResetEvent("Wake event") };
  std::atomic<std::size_t> published{ 0                              };
  std::atomic<std::size_t> consumed { 0                              };
  std::jthread producer([&](std::stop_token const& stop) { ProducePending(wake, published, consumed, stop); });
  ASSERT_NO_FATAL_FAILURE(ConsumePublished(wake, published, consumed));
  producer.request_stop();
  producer.join();
  wake.Transition(Phase::Idle);
  EXPECT_FALSE(Waitable{ wake.get() }.Signalled());
  // Reproduce an event set after a consumer observed Idle, before it stored Idle.
  ASSERT_TRUE(SetEvent(wake.get()));
  wake.Transition(Phase::Idle);
  EXPECT_FALSE(Waitable{ wake.get() }.Signalled());
}
}
}
