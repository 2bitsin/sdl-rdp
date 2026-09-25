#include <sdl-rdp/freerdp-facade/manual-reset-event.hpp>

#include <sdl-rdp/freerdp-facade/wake-event.hpp>

#include <gtest/gtest.h>
#include <winpr/synch.h>

TEST(ManualResetEvent, StartsClearAndStaysSetUntilReset) {
  auto const event = sdl_rdp::freerdp_facade::ManualResetEvent("Test event");
  ASSERT_TRUE(event);
  EXPECT_FALSE(Backend::Signalled(event.get()));
  ASSERT_TRUE(SetEvent(event.get()));
  EXPECT_TRUE(Backend::Signalled(event.get()));
  EXPECT_TRUE(Backend::Signalled(event.get()));
  ASSERT_TRUE(ResetEvent(event.get()));
  EXPECT_FALSE(Backend::Signalled(event.get()));
}
