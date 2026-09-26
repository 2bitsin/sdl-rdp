#include <sdl-rdp/freerdp-facade/signalled.hpp>

#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>

#include <gtest/gtest.h>
#include <winpr/synch.h>
#include <array>
#include <optional>

namespace sdl_rdp::freerdp_facade::detail::signalled {
TEST(Signalled, HoldsTheHandlesSignalledAtConstruction) {
  auto const set   = ManualResetEvent("Set event");
  auto const clear = ManualResetEvent("Clear event");
  ASSERT_TRUE(SetEvent(set.get()));
  auto const fired = Signalled{ std::array{ WaitHandle{ set }, WaitHandle{ clear } } };
  ASSERT_TRUE(ResetEvent(set.get()));
  EXPECT_TRUE(fired.Contains(WaitHandle{ set }));
  EXPECT_FALSE(fired.Contains(WaitHandle{ clear }));
}
TEST(Signalled, AnAbsentHandleNeverFired) {
  auto const set = ManualResetEvent("Present event");
  ASSERT_TRUE(SetEvent(set.get()));
  auto const fired = Signalled{ std::array{ WaitHandle{ set } } };
  EXPECT_TRUE(fired.Contains(std::optional{ WaitHandle{ set } }));
  EXPECT_FALSE(fired.Contains(std::optional<WaitHandle>{ }));
}
}
