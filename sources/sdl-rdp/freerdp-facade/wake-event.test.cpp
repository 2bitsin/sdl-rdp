#include <sdl-rdp/freerdp-facade/wake-event.hpp>

#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <optional>
#include <ranges>
#include <thread>
#include <utility>

namespace sdl_rdp::freerdp_facade::detail::wake_event {
class WakeEventProbe {
public:
  static auto SetBehindPhase(WakeEvent const& wake) -> void {
    wake.handle.Set();
  }
};
namespace {
auto ConsumePublished(WakeEvent& wake, std::atomic<std::size_t>& published, std::atomic<std::size_t>& consumed)
    -> void {
  for (int iteration = 0; iteration < 4096; ++iteration) {
    wake.Transition(WakeEvent::Phase::Idle);
    if (published.load() == consumed.load())
      ASSERT_EQ(WaitHandle::Any(std::array{ wake.Handle() }, 10000), std::optional<std::size_t>{ 0 });
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
  EXPECT_FALSE(wake.Handle().Signalled());
  // Reproduce an event set after a consumer observed Idle, before it stored Idle.
  WakeEventProbe::SetBehindPhase(wake);
  wake.Transition(Phase::Idle);
  EXPECT_FALSE(wake.Handle().Signalled());
}
}
}
