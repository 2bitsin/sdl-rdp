#include <sdl-rdp/freerdp-facade/rdpdr.hpp>

#include <gtest/gtest.h>

namespace sdl_rdp::freerdp_facade::detail::rdpdr {
namespace {
TEST(NtStatusName, NamesAKnownStatus) {
  EXPECT_EQ(Name(NtStatus::NoMoreFiles), "STATUS_NO_MORE_FILES");
}
TEST(NtStatusName, NamesAnUnknownStatusAsUnknown) {
  EXPECT_EQ(Name(NtStatus{ 0xE0001234 }), "unknown NTSTATUS");
}
}
}
