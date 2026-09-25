#include <sdl-rdp/freerdp-facade/handled.hpp>

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <format>
#include <functional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
struct Pdu {
  int value;
};
class Owner {
public:
  auto Failures(std::string_view operation) -> std::function<void(std::string_view)> {
    return [this, operation](std::string_view text) { _reports.push_back(std::format("{}: {}", operation, text)); };
  }
  auto Take(Pdu const& pdu) const -> int {
    if (pdu.value < _floor) throw std::runtime_error("negative");
    return pdu.value;
  }
  auto Flag(int flags) const -> bool {
    return flags != _floor;
  }
  auto Reports() const -> std::vector<std::string> const& {
    return _reports;
  }

private:
  std::vector<std::string> _reports;
  int                      _floor  { };
};
struct Context {
  std::reference_wrapper<Owner> owner;
};
auto Held(Context const& context) -> Owner& {
  return context.owner.get();
}
constexpr std::string_view Operation = "Probe";
using PduSlot  = auto (*)(Context*, Pdu const*) noexcept -> int;
using FlagSlot = auto (*)(Context*, int, Pdu const*) noexcept -> int;
using UserSlot = auto (*)(void*, int) noexcept -> int;
using VoidSlot = auto (*)(Context*, Pdu const*) noexcept -> void;
using HashSlot = auto (*)(Context*, std::uint8_t*) noexcept -> int;
using sdl_rdp::freerdp_facade::Handled;
using sdl_rdp::freerdp_facade::Itself;
}
TEST(Handled, PassesThePduAsAReference) {
  Owner         owner;
  Context       context { owner };
  Pdu const     pdu     { 7     };
  PduSlot const slot    = Handled<Held, &Owner::Take, Operation, &Owner::Failures, -1>;
  EXPECT_EQ(slot(&context, &pdu), 7);
  EXPECT_TRUE(owner.Reports().empty());
}
TEST(Handled, TurnsAThrowIntoTheFailureReportedUnderTheOperation) {
  Owner         owner;
  Context       context { owner };
  Pdu const     pdu     { -1    };
  PduSlot const slot    = Handled<Held, &Owner::Take, Operation, &Owner::Failures, -1>;
  EXPECT_EQ(slot(&context, &pdu), -1);
  EXPECT_EQ(owner.Reports(), std::vector<std::string>{ "Probe: negative" });
}
TEST(Handled, PassesOnlyTheLeadingArgumentsTheHandlerTakes) {
  Owner          owner;
  Context        context { owner };
  FlagSlot const slot    = Handled<Held, &Owner::Flag, Operation, &Owner::Failures, false>;
  EXPECT_EQ(slot(&context, 1, nullptr), 1);
  EXPECT_EQ(slot(&context, 0, nullptr), 0);
}
TEST(Handled, TakesALambdaHandlerThatReceivesTheOwnerFirst) {
  Owner          owner;
  Context        context { owner };
  FlagSlot const slot    = Handled<Held, [](Owner const& found, int flags) { return found.Flag(flags) ? 5 : 0; },
                                Operation, &Owner::Failures, -1>;
  EXPECT_EQ(slot(&context, 3, nullptr), 5);
}
TEST(Handled, FindsTheOwnerARegistrationHandsBackAsUserData) {
  Owner          owner;
  UserSlot const slot  = Handled<Itself<Owner>, &Owner::Flag, Operation, &Owner::Failures, -1>;
  EXPECT_EQ(slot(&owner, 1), 1);
}
TEST(Handled, ReportsAThrowFromAVoidSlot) {
  Owner          owner;
  Context        context { owner };
  Pdu const      pdu     { -1    };
  VoidSlot const slot    = Handled<Held, &Owner::Take, Operation, &Owner::Failures>;
  slot(&context, &pdu);
  EXPECT_EQ(owner.Reports(), std::vector<std::string>{ "Probe: negative" });
}
TEST(Handled, HandsAFixedExtentBufferAsTheSpanTheHandlerTakes) {
  Owner                       owner;
  Context                     context { owner      };
  std::array<std::uint8_t, 4> buffer  { 1, 2, 3, 4 };
  constexpr auto sum = [](Owner const&, std::span<std::uint8_t, 4> bytes) -> int { return bytes[0] + bytes[3]; };
  HashSlot const              slot    = Handled<Held, sum, Operation, &Owner::Failures, -1>;
  EXPECT_EQ(slot(&context, buffer.data()), 5);
}
