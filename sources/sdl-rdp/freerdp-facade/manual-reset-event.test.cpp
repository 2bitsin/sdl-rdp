#include <sdl-rdp/freerdp-facade/manual-reset-event.hpp>

#include <sdl-rdp/freerdp-facade/waitable.hpp>

#include <gtest/gtest.h>
#include <winpr/synch.h>

TEST(ManualResetEvent, StartsClearAndStaysSetUntilReset) {
  auto const event = sdl_rdp::freerdp_facade::ManualResetEvent("Test event");
  ASSERT_TRUE(event);
  EXPECT_FALSE(sdl_rdp::freerdp_facade::Waitable{ event.get() }.Signalled());
  ASSERT_TRUE(SetEvent(event.get()));
  EXPECT_TRUE(sdl_rdp::freerdp_facade::Waitable{ event.get() }.Signalled());
  EXPECT_TRUE(sdl_rdp::freerdp_facade::Waitable{ event.get() }.Signalled());
  ASSERT_TRUE(ResetEvent(event.get()));
  EXPECT_FALSE(sdl_rdp::freerdp_facade::Waitable{ event.get() }.Signalled());
}
