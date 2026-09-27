#include <sdl-rdp/freerdp-facade/wait-handle.hpp>

#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>

#include <gtest/gtest.h>
#include <array>
#include <cstddef>
#include <optional>

namespace sdl_rdp::freerdp_facade::detail::wait_handle {
TEST(WaitHandle, AnyNamesTheSignalledIndexOrTimesOut) {
  auto const first   = ManualResetEvent("First awaited event");
  auto const second  = ManualResetEvent("Second awaited event");
  auto const handles = std::array{ WaitHandle{ first }, WaitHandle{ second } };
  EXPECT_EQ(WaitHandle::Any(handles, 0), std::nullopt);
  second.Set();
  EXPECT_EQ(WaitHandle::Any(handles, 0), std::optional<std::size_t>{ 1 });
  first.Set();
  EXPECT_EQ(WaitHandle::Any(handles, Forever), std::optional<std::size_t>{ 0 });
}
}
