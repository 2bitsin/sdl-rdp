#include <sdl-rdp/freerdp-facade/touch-channel.hpp>

#include <sdl-rdp/freerdp-facade/support.test/recorded-assignee.hpp>
#include <sdl-rdp/freerdp-facade/support.test/recorded-failures.hpp>
#include <sdl-rdp/freerdp-facade/support.test/unjoined-connection.hpp>

#include <freerdp/server/rdpei.h>
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::touch_channel {
class TouchChannelProbe {
public:
  static auto Context(TouchChannel const& channel) -> RdpeiServerContext& {
    return channel.Context();
  }
};
namespace {
using sdl_rdp::freerdp_facade::support_test::RecordedAssignee;
using sdl_rdp::freerdp_facade::support_test::RecordedFailures;
using sdl_rdp::freerdp_facade::support_test::UnjoinedConnection;

struct Recorded {
  bool                      throws  { };
  std::vector<TouchContact> contacts;
  std::vector<std::string>  failures;
};
class Recorder final : public RecordedFailures<TouchChannelEvents> {
public:
  explicit Recorder(Recorded& recorded) : RecordedFailures{ recorded.failures }, _recorded{ recorded } { }
  auto     Touch(std::span<TouchContact const> contacts) -> void override {
    if (_recorded.throws) throw std::runtime_error{ "refused" };
    _recorded.contacts.assign(contacts.begin(), contacts.end());
  }

private:
  Recorded& _recorded;
};
auto Contact(std::uint32_t id, std::uint32_t flags, std::optional<std::uint32_t> pressure = std::nullopt)
    -> RDPINPUT_CONTACT_DATA {
  RDPINPUT_CONTACT_DATA contact{ };
  contact.contactId    = id;
  contact.x            = static_cast<std::int32_t>(10 * id);
  contact.y            = static_cast<std::int32_t>(20 * id);
  contact.contactFlags = flags;
  if (pressure) {
    contact.fieldsPresent = CONTACT_DATA_PRESSURE_PRESENT;
    contact.pressure      = *pressure;
  }
  return contact;
}
class UnopenedTouch : public testing::Test {
protected:
  Recorded           recorded;
  Recorder           events  { recorded                            };
  RecordedAssignee   assignee{ recorded.failures                   };
  UnjoinedConnection unjoined;
  TouchChannel       channel { unjoined.channels, events, assignee };
};
// The open fails while the client has no dynamic channels, but the context and its slots are in place.
class TouchSlots : public UnopenedTouch {
protected:
  auto Sent(std::span<RDPINPUT_TOUCH_FRAME> frames) -> std::uint32_t {
    RDPINPUT_TOUCH_EVENT event{ };
    event.frameCount = static_cast<std::uint16_t>(frames.size());
    event.frames     = frames.data();
    return context.onTouchEvent(&context, &event);
  }
  bool                opened { channel.Open()                      };
  RdpeiServerContext& context{ TouchChannelProbe::Context(channel) };
};
auto Frame(std::span<RDPINPUT_CONTACT_DATA> contacts) -> RDPINPUT_TOUCH_FRAME {
  RDPINPUT_TOUCH_FRAME frame{ };
  frame.contactCount = static_cast<std::uint16_t>(contacts.size());
  frame.contacts     = contacts.data();
  return frame;
}
}
TEST_F(TouchSlots, EveryContactOfEveryFrameReachesTheHandlerInOrder) {
  std::array first { Contact(1, RDPINPUT_CONTACT_FLAG_DOWN, 512), Contact(2, RDPINPUT_CONTACT_FLAG_UPDATE) };
  std::array second{ Contact(3, RDPINPUT_CONTACT_FLAG_UP | RDPINPUT_CONTACT_FLAG_CANCELED),
                     Contact(4, RDPINPUT_CONTACT_FLAG_UP) };
  std::array frames{ Frame(first), Frame(second)                                                           };
  EXPECT_EQ(Sent(frames), CHANNEL_RC_OK);
  ASSERT_EQ(recorded.contacts.size(), 4U);
  EXPECT_EQ(recorded.contacts[0].id, 1U);
  EXPECT_EQ(recorded.contacts[0].x, 10);
  EXPECT_EQ(recorded.contacts[0].y, 20);
  EXPECT_EQ(recorded.contacts[0].phase, ContactPhase::Down);
  EXPECT_EQ(recorded.contacts[0].pressure, 512U);
  EXPECT_EQ(recorded.contacts[1].phase, ContactPhase::Move);
  EXPECT_EQ(recorded.contacts[1].pressure, std::nullopt);
  EXPECT_EQ(recorded.contacts[2].phase, ContactPhase::Cancel);
  EXPECT_EQ(recorded.contacts[3].id, 4U);
  EXPECT_EQ(recorded.contacts[3].phase, ContactPhase::Up);
}
TEST_F(TouchSlots, AThrowingHandlerIsAnInternalErrorReportedOnce) {
  recorded.throws = true;
  std::array contacts{ Contact(1, RDPINPUT_CONTACT_FLAG_DOWN) };
  std::array frames  { Frame(contacts)                        };
  EXPECT_EQ(Sent(frames), ERROR_INTERNAL_ERROR);
  EXPECT_EQ(recorded.failures, std::vector<std::string>{ "Touch event" });
}
TEST_F(TouchSlots, TheAssignedIdReachesTheAssignee) {
  EXPECT_TRUE(context.onChannelIdAssigned(&context, 7));
  EXPECT_EQ(assignee.Assigned(), 7U);
}
TEST_F(TouchSlots, AThrowingAssigneeRefusesTheId) {
  assignee.Refuse();
  EXPECT_FALSE(context.onChannelIdAssigned(&context, 7));
  EXPECT_EQ(recorded.failures, std::vector<std::string>{ "Touch channel assignment" });
}
TEST_F(TouchSlots, ARefusedOpenKeepsTheSlotsInstalled) {
  EXPECT_FALSE(opened);
  EXPECT_NE(context.onTouchEvent, nullptr);
  EXPECT_NE(context.onChannelIdAssigned, nullptr);
}
TEST_F(UnopenedTouch, OpeningBeforeTheClientJoinsTheChannelIsRefused) {
  EXPECT_FALSE(channel.Open());
}
TEST_F(UnopenedTouch, AnUnopenedChannelHasNoHandle) {
  EXPECT_DEATH(std::ignore = channel.Handle(), "the touch channel has its context");
}
}
