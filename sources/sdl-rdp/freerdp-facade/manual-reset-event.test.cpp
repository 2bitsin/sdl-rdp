#include <sdl-rdp/freerdp-facade/manual-reset-event.hpp>

#include <sdl-rdp/freerdp-facade/waitable.hpp>

#include <gtest/gtest.h>
#include <winpr/synch.h>

namespace sdl_rdp::freerdp_facade::detail::manual_reset_event {
TEST(ManualResetEvent, StartsClearAndStaysSetUntilReset) {
  auto const event = ManualResetEvent("Test event");
  ASSERT_TRUE(event);
  EXPECT_FALSE(Waitable{ event.get() }.Signalled());
  ASSERT_TRUE(SetEvent(event.get()));
  EXPECT_TRUE(Waitable{ event.get() }.Signalled());
  EXPECT_TRUE(Waitable{ event.get() }.Signalled());
  ASSERT_TRUE(ResetEvent(event.get()));
  EXPECT_FALSE(Waitable{ event.get() }.Signalled());
}
}
