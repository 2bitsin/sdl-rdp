#include <sdl-rdp/freerdp-facade/channel-manager.hpp>

#include <sdl-rdp/freerdp-facade/connection.test.hpp>
#include <sdl-rdp/freerdp-facade/exceptions.hpp>
#include <sdl-rdp/freerdp-facade/support.test/unjoined-connection.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <freerdp/channels/rdpdr.h>
#include <freerdp/server/rdpsnd.h>
#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
#include <tuple>

namespace sdl_rdp::freerdp_facade::detail::channel_manager {
namespace {
using sdl_rdp::freerdp_facade::support_test::UnjoinedConnection;
using sdl_rdp::utilities::Releases;
using SoundContext = std::unique_ptr<RdpsndServerContext, Releases<rdpsnd_server_context_free>>;

constexpr std::uint32_t Installed = 7;
constexpr auto          Install   = [](RdpsndServerContext& context) { context.latency = Installed; };
class Unjoined : public testing::Test {
protected:
  UnjoinedConnection unjoined;
  ChannelManager&    manager { unjoined.channels };
};
}
TEST_F(Unjoined, NoStaticChannelIsJoinedBeforeTheClientJoinsIt) {
  EXPECT_FALSE(manager.Joined(RDPSND_CHANNEL_NAME));
  EXPECT_FALSE(manager.Joined(RDPDR_SVC_CHANNEL_NAME));
}
TEST_F(Unjoined, OpeningAnUnjoinedChannelThrows) {
  EXPECT_THROW(std::ignore = manager.Open(RDPDR_SVC_CHANNEL_NAME), ChannelOpenFailed);
}
TEST_F(Unjoined, ReclaimingAnUnjoinedChannelNeverThrows) {
  static_assert(noexcept(manager.Reclaim(RDPSND_CHANNEL_NAME)));
  manager.Reclaim(RDPSND_CHANNEL_NAME);
}
TEST_F(Unjoined, TheDynamicChannelIsNotReadyBeforeActivation) {
  EXPECT_FALSE(manager.DynamicReady());
}
TEST_F(Unjoined, ANameLongerThanTheProtocolFieldIsAContractFailure) {
  EXPECT_DEATH(std::ignore = manager.Joined("longerthan8"), "fits its protocol field");
}
TEST_F(Unjoined, ABoundContextCarriesItsOwnerAndItsSessionWithItsSlotsInstalled) {
  int owner{ };
  auto const bound{ manager.Bound<SoundContext, rdpsnd_server_context_new, &RdpsndServerContext::data, Install, int>(
      owner) };
  EXPECT_EQ(bound->data, &owner);
  EXPECT_EQ(bound->rdpcontext, &ConnectionProbe::Context(unjoined.connection));
  EXPECT_EQ(bound->latency, Installed);
}
}
