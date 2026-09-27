#include <sdl-rdp/freerdp-facade/display-channel.hpp>

#include <sdl-rdp/freerdp-facade/support.test/recorded-failures.hpp>
#include <sdl-rdp/freerdp-facade/support.test/unjoined-connection.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/server/disp.h>
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::display_channel {
class DisplayChannelProbe {
public:
  static auto Context(DisplayChannel const& channel) -> DispServerContext& {
    return channel.Context();
  }
};
namespace {
using sdl_rdp::freerdp_facade::support_test::RecordedFailures;
using sdl_rdp::freerdp_facade::support_test::UnjoinedConnection;
using sdl_rdp::utilities::Narrowed;

struct Recorded {
  bool                         accepts { true };
  bool                         throws  { };
  std::optional<std::uint32_t> assigned;
  std::vector<DisplayMonitor>  monitors;
  std::vector<std::string>     failures;
};
class Recorder final : public RecordedFailures<DisplayChannelEvents> {
public:
  explicit Recorder(Recorded& recorded) : RecordedFailures{ recorded.failures }, _recorded{ recorded } { }
  auto     ChannelAssigned(std::uint32_t id) -> void override {
    _recorded.assigned = id;
  }
  auto MonitorLayout(std::span<DisplayMonitor const> monitors) -> bool override {
    if (_recorded.throws) throw std::runtime_error{ "refused" };
    _recorded.monitors.assign(monitors.begin(), monitors.end());
    return _recorded.accepts;
  }

private:
  Recorded& _recorded;
};
class UnopenedDisplay : public testing::Test {
protected:
  Recorded           recorded;
  Recorder           events  { recorded                  };
  UnjoinedConnection unjoined;
  DisplayChannel     channel { unjoined.channels, events };
};
// The open fails while the client has no dynamic channels, but the context and its slots are in place.
class DisplaySlots : public UnopenedDisplay {
protected:
  bool               opened { channel.Open({ .max_monitors = 16 })  };
  DispServerContext& context{ DisplayChannelProbe::Context(channel) };
};
auto Monitor(std::int32_t left, std::int32_t top, std::uint32_t width, std::uint32_t height)
    -> DISPLAY_CONTROL_MONITOR_LAYOUT {
  DISPLAY_CONTROL_MONITOR_LAYOUT monitor{ };
  monitor.Left   = left;
  monitor.Top    = top;
  monitor.Width  = width;
  monitor.Height = height;
  return monitor;
}
auto Layout(std::span<DISPLAY_CONTROL_MONITOR_LAYOUT> monitors) -> DISPLAY_CONTROL_MONITOR_LAYOUT_PDU {
  DISPLAY_CONTROL_MONITOR_LAYOUT_PDU pdu{ };
  pdu.NumMonitors = Narrowed<std::uint32_t>(monitors.size());
  pdu.Monitors    = monitors.data();
  return pdu;
}
}
TEST_F(UnopenedDisplay, AdvertisingCapsBeforeOpeningIsAContractFailure) {
  EXPECT_DEATH(std::ignore = channel.Caps(), "the display channel is open");
}
TEST_F(UnopenedDisplay, AChannelOpensOnce) {
  std::ignore = channel.Open({ });
  EXPECT_DEATH(std::ignore = channel.Open({ }), "display control opens once");
}
TEST_F(UnopenedDisplay, AClosedChannelOpensAgain) {
  EXPECT_FALSE(channel.Open({ }));
  channel.Close();
  EXPECT_FALSE(channel.Open({ }));
}
TEST_F(DisplaySlots, ALayoutReachesTheHandlerMonitorByMonitor) {
  std::array monitors{ Monitor(-1920, 0, 1920, 1080), Monitor(0, -200, 2560, 1440) };
  auto const pdu     { Layout(monitors)                                            };
  EXPECT_EQ(context.DispMonitorLayout(&context, &pdu), CHANNEL_RC_OK);
  ASSERT_EQ(recorded.monitors.size(), 2U);
  EXPECT_EQ(recorded.monitors[0].left, -1920);
  EXPECT_EQ(recorded.monitors[0].top, 0);
  EXPECT_EQ(recorded.monitors[0].width, 1920U);
  EXPECT_EQ(recorded.monitors[0].height, 1080U);
  EXPECT_EQ(recorded.monitors[1].left, 0);
  EXPECT_EQ(recorded.monitors[1].top, -200);
  EXPECT_EQ(recorded.monitors[1].width, 2560U);
  EXPECT_EQ(recorded.monitors[1].height, 1440U);
}
TEST_F(DisplaySlots, ARefusedLayoutIsInvalidData) {
  recorded.accepts = false;
  std::array monitors{ Monitor(0, 0, 800, 600) };
  auto const pdu     { Layout(monitors)        };
  EXPECT_EQ(context.DispMonitorLayout(&context, &pdu), ERROR_INVALID_DATA);
}
TEST_F(DisplaySlots, TheAssignedIdReachesTheHandler) {
  EXPECT_TRUE(context.ChannelIdAssigned(&context, 7));
  EXPECT_EQ(recorded.assigned, 7U);
}
TEST_F(DisplaySlots, AThrowingLayoutHandlerIsAnInternalErrorReportedOnce) {
  recorded.throws = true;
  std::array monitors{ Monitor(0, 0, 800, 600) };
  auto const pdu     { Layout(monitors)        };
  EXPECT_EQ(context.DispMonitorLayout(&context, &pdu), ERROR_INTERNAL_ERROR);
  EXPECT_EQ(recorded.failures, std::vector<std::string>{ "Display layout" });
}
}
