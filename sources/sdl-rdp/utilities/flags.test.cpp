#include <sdl-rdp/utilities/flags.hpp>

#include <gtest/gtest.h>
#include <cstdint>
#include <type_traits>

namespace sdl_rdp::utilities::detail::flags {
namespace {
enum class Access : std::uint16_t { None = 0, Read = 0x1, Write = 0x2, Delete = 0x8000 };
auto FlagSet(Access /*set*/) -> std::true_type;
enum class Plain  : std::uint16_t { One = 0x1 };
enum class Signed : std::int16_t { One = 0x1 };
auto FlagSet(Signed /*set*/) -> std::true_type;
}
TEST(Flags, OnlyOptedInUnsignedScopedEnumsAreFlagSets) {
  static_assert(FlagEnum<Access>);
  static_assert(!FlagEnum<Plain>);
  static_assert(!FlagEnum<Signed>);
  static_assert(!FlagEnum<std::uint16_t>);
}
TEST(Flags, JoinsFlagsIntoTheirEnum) {
  static_assert(std::is_same_v<decltype(Access::Read | Access::Write), Access>);
  static_assert(std::to_underlying(Access::Read | Access::Write | Access::Delete) == 0x8003);
  static_assert(((Access::Read | Access::Delete) & Access::Delete) == Access::Delete);
  static_assert(((Access::Read | Access::Delete) & Access::Write) == Access::None);
}
TEST(Flags, FindsAFlagInASet) {
  static_assert(Has(Access::Read | Access::Delete, Access::Delete));
  static_assert(!Has(Access::Read | Access::Delete, Access::Write));
  static_assert(!Has(Access::None, Access::Read));
}
}
