#include <sdl-rdp/freerdp-facade/advanced-input-channel.hpp>

#include <sdl-rdp/freerdp-facade/support.test/recorded-assignee.hpp>
#include <sdl-rdp/freerdp-facade/support.test/recorded-failures.hpp>
#include <sdl-rdp/freerdp-facade/support.test/unjoined-connection.hpp>

#include <freerdp/channels/wtsvc.h>
#include <freerdp/server/ainput.h>
#include <gtest/gtest.h>
#include <winpr/synch.h>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::advanced_input_channel {
class AdvancedInputChannelProbe {
public:
  static auto Context(AdvancedInputChannel const& channel) -> ainput_server_context& {
    return channel.Context();
  }
};
namespace {
using sdl_rdp::freerdp_facade::support_test::RecordedAssignee;
using sdl_rdp::freerdp_facade::support_test::RecordedFailures;
using sdl_rdp::freerdp_facade::support_test::UnjoinedConnection;

// ainput waits up to 1 s on the manager's manual-reset event before its first open (FreeRDP 3.32 ainput_main.c:113).
auto Signalled(ChannelManager& channels) -> bool {
  auto const throwaway = channels.Create<AdvancedInputContext, ainput_server_context_new>();
  return throwaway && SetEvent(WTSVirtualChannelManagerGetEventHandle(throwaway->vcm));
}

struct Recorded {
  bool                              accepts { true };
  std::vector<AdvancedPointerEvent> pointers;
  std::vector<std::string>          failures;
};
class Recorder final : public RecordedFailures<AdvancedInputChannelEvents> {
public:
  explicit Recorder(Recorded& recorded) : RecordedFailures{ recorded.failures }, _recorded{ recorded } { }
  auto     AdvancedPointer(AdvancedPointerEvent const& event) -> bool override {
    _recorded.pointers.push_back(event);
    return _recorded.accepts;
  }

private:
  Recorded& _recorded;
};
class UnopenedAdvancedInput : public testing::Test {
protected:
  Recorded             recorded;
  Recorder             events   { recorded                                                 };
  RecordedAssignee     assignee { recorded.failures                                        };
  UnjoinedConnection   unjoined;
  bool                 signalled{ Signalled(unjoined.channels)                             };
  AdvancedInputChannel channel  { unjoined.channels, unjoined.connection, events, assignee };
};
// The open fails while the client has no dynamic channels, but the context and its slots are in place.
class AdvancedInputSlots : public UnopenedAdvancedInput {
protected:
  auto Sent(std::uint64_t flags, std::int32_t x, std::int32_t y) -> std::uint32_t {
    return context.MouseEvent(&context, 0, flags, x, y);
  }
  auto Last() const -> AdvancedPointerEvent {
    EXPECT_FALSE(recorded.pointers.empty());
    return recorded.pointers.empty() ? AdvancedPointerEvent{ } : recorded.pointers.back();
  }
  auto Buttons() const -> std::string {
    return Last().buttons.to_string();
  }
  bool                   opened { channel.Open()                              };
  ainput_server_context& context{ AdvancedInputChannelProbe::Context(channel) };
};
}
TEST_F(AdvancedInputSlots, ButtonsMapLeftMiddleRightBackForward) {
  EXPECT_EQ(Sent(AINPUT_FLAGS_BUTTON1 | AINPUT_FLAGS_DOWN, 0, 0), CHANNEL_RC_OK);
  EXPECT_EQ(Buttons(), "00001");
  EXPECT_TRUE(Last().down);
  std::ignore = Sent(AINPUT_FLAGS_BUTTON3, 0, 0);
  EXPECT_EQ(Buttons(), "00010");
  EXPECT_FALSE(Last().down);
  std::ignore = Sent(AINPUT_FLAGS_BUTTON2, 0, 0);
  EXPECT_EQ(Buttons(), "00100");
  std::ignore = Sent(AINPUT_XFLAGS_BUTTON1, 0, 0);
  EXPECT_EQ(Buttons(), "01000");
  std::ignore = Sent(AINPUT_XFLAGS_BUTTON2, 0, 0);
  EXPECT_EQ(Buttons(), "10000");
}
TEST_F(AdvancedInputSlots, RelativeMotionCarriesItsDelta) {
  std::ignore = Sent(AINPUT_FLAGS_MOVE | AINPUT_FLAGS_REL, -3, 4);
  EXPECT_TRUE(Last().moved);
  EXPECT_TRUE(Last().relative);
  EXPECT_TRUE(Last().relative_capable);
  EXPECT_EQ(std::pair(Last().x, Last().y), std::pair(-3, 4));
  EXPECT_EQ(Last().wheel, std::nullopt);
}
TEST_F(AdvancedInputSlots, AbsoluteMotionReportsWhetherTheClientCanSendRelative) {
  std::ignore = Sent(AINPUT_FLAGS_MOVE | AINPUT_FLAGS_HAVE_REL, 100, 200);
  EXPECT_FALSE(Last().relative);
  EXPECT_TRUE(Last().relative_capable);
  std::ignore = Sent(AINPUT_FLAGS_MOVE, 100, 200);
  EXPECT_FALSE(Last().relative_capable);
}
TEST_F(AdvancedInputSlots, AWheelTurnIsInNotches) {
  constexpr std::int32_t notch = 120 * 65536;
  std::ignore = Sent(AINPUT_FLAGS_WHEEL, notch, -2 * notch);
  EXPECT_TRUE(Last().wheel.has_value());
  EXPECT_FLOAT_EQ(Last().wheel.value_or(WheelTurn{ }).horizontal, 1.0F);
  EXPECT_FLOAT_EQ(Last().wheel.value_or(WheelTurn{ }).vertical, -2.0F);
}
TEST_F(AdvancedInputSlots, ARefusedEventIsAnInternalErrorAndNoFailure) {
  recorded.accepts = false;
  EXPECT_EQ(Sent(AINPUT_FLAGS_MOVE, 1, 1), ERROR_INTERNAL_ERROR);
  EXPECT_TRUE(recorded.failures.empty());
}
TEST_F(AdvancedInputSlots, TheAssignedIdReachesTheAssignee) {
  EXPECT_TRUE(context.ChannelIdAssigned(&context, 9));
  EXPECT_EQ(assignee.Assigned(), 9U);
}
TEST_F(AdvancedInputSlots, AThrowingAssigneeRefusesTheId) {
  assignee.Refuse();
  EXPECT_FALSE(context.ChannelIdAssigned(&context, 9));
  EXPECT_EQ(recorded.failures, std::vector<std::string>{ "Advanced input channel assignment" });
}
TEST_F(AdvancedInputSlots, ARefusedOpenKeepsTheSlotsInstalled) {
  EXPECT_FALSE(opened);
  EXPECT_NE(context.MouseEvent, nullptr);
  EXPECT_NE(context.ChannelIdAssigned, nullptr);
}
TEST_F(UnopenedAdvancedInput, OpeningBeforeTheClientJoinsTheChannelIsRefused) {
  ASSERT_TRUE(signalled);
  EXPECT_FALSE(channel.Open());
}
TEST_F(UnopenedAdvancedInput, AnUnopenedChannelHasNoHandle) {
  EXPECT_DEATH(std::ignore = channel.Handle(), "the advanced input channel has its context");
}
}
