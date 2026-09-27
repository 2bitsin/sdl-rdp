#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>

#include <sdl-rdp/freerdp-facade/wait-handle.hpp>

#include <gtest/gtest.h>
#include <array>
#include <optional>
#include <tuple>

namespace sdl_rdp::freerdp_facade::detail::rdp_handles {
TEST(EventHandle, ResetUnsignalsASetEvent) {
  auto const event   = ManualResetEvent("Reset event");
  auto const handles = std::array{ WaitHandle{ event } };
  event.Set();
  event.Reset();
  EXPECT_EQ(WaitHandle::Any(handles, 0), std::nullopt);
}
TEST(EventHandleDeathTest, OwnsAnEvent) {
  EXPECT_DEATH(std::ignore = EventHandle{ OwnedEvent{ } }, "an event handle owns an event");
}
}
