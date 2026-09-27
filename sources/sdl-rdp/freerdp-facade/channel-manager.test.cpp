#include <sdl-rdp/freerdp-facade/channel-manager.hpp>

#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/freerdp-facade/exceptions.hpp>
#include <sdl-rdp/utilities/posix.hpp>

#include <freerdp/channels/rdpdr.h>
#include <freerdp/server/rdpsnd.h>
#include <gtest/gtest.h>
#include <tuple>
#include <utility>

namespace sdl_rdp::freerdp_facade::detail::channel_manager {
namespace {
using sdl_rdp::utilities::ConnectedSockets;
using sdl_rdp::utilities::SocketPair;

class Unjoined : public testing::Test {
protected:
  SocketPair     sockets   { ConnectedSockets()        };
  Connection     connection{ std::move(sockets.server) };
  ChannelManager manager   { connection.Context()      };
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
}
