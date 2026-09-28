#include <sdl-rdp/utilities/socket.hpp>

#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

namespace sdl_rdp::utilities::detail::socket {
namespace {
constexpr Ipv4Address      Loopback{ 127, 0, 0, 1 };
constexpr std::string_view Greeting{ "hello"      };

auto Received(Socket const& socket) -> std::string {
  std::array<char, 16> buffer { };
  auto const           count  = socket.Receive(buffer);
  return count < 0 ? std::string{ } : std::string(buffer.data(), static_cast<std::size_t>(count));
}
}
TEST(ConnectedSockets, CarryBytesBothWays) {
  auto const pair = ConnectedSockets();
  EXPECT_EQ(pair.client.Send(Greeting), std::ssize(Greeting));
  EXPECT_EQ(Received(pair.server), Greeting);
  EXPECT_EQ(pair.server.Send(Greeting), std::ssize(Greeting));
  EXPECT_EQ(Received(pair.client), Greeting);
}
TEST(Socket, ShutdownOfSendingIsTheEndOfTheStreamForThePeer) {
  auto const pair = ConnectedSockets();
  EXPECT_TRUE(pair.client.Shutdown(Socket::Direction::Send));
  std::array<char, 1> buffer{ };
  EXPECT_EQ(pair.server.Receive(buffer), 0);
}
TEST(Socket, BlockedReceiveEndsAtTheLimit) {
  auto const pair = ConnectedSockets();
  pair.server.LimitBlockedCalls(std::chrono::milliseconds{ 20 });
  std::array<char, 1> buffer{ };
  EXPECT_EQ(pair.server.Receive(buffer), -1);
  auto const error = LastSocketError();
  // POSIX reports the limit as EAGAIN, Winsock as WSAETIMEDOUT.
  EXPECT_TRUE(error == std::errc::resource_unavailable_try_again || error == std::errc::timed_out) << error.message();
}
TEST(Socket, MoveAndReleaseHandTheSocketOn) {
  auto   pair   = ConnectedSockets();
  auto   native = pair.server.Native();
  Socket moved  { std::move(pair.server) };
  EXPECT_FALSE(pair.server.Owns());
  EXPECT_EQ(moved.Native(), native);
  EXPECT_EQ(moved.Release(), native);
  EXPECT_FALSE(moved.Owns());
  Socket const readopted{ native, SocketLibrary{ } };
  EXPECT_EQ(readopted.Native(), native);
}
TEST(ListeningSocket, TakesAnEphemeralPortAndHoldsItAlone) {
  auto const listener = ListeningSocket({ .address = Loopback, .port = 0 }, 1);
  EXPECT_NE(listener.Port(), 0);
  EXPECT_THROW(std::ignore = ListeningSocket({ .address = Loopback, .port = listener.Port() }, 1), std::system_error);
}
TEST(ParsedIpv4, ReadsADottedQuadInNetworkOrder) {
  EXPECT_EQ(ParsedIpv4("127.0.0.1"), Loopback);
  EXPECT_EQ(ParsedIpv4("0.0.0.0"), (Ipv4Address{ 0, 0, 0, 0 }));
  EXPECT_EQ(ParsedIpv4("1.2.3.4"), (Ipv4Address{ 1, 2, 3, 4 }));
}
TEST(ParsedIpv4, RefusesAnythingElse) {
  constexpr std::array<std::string_view, 7> refused{ "localhost", "1.2.3", "256.0.0.1", "::1",
                                                     "", "010.0.0.1", "0x7f.0.0.1" };
  for (auto const text : refused) EXPECT_EQ(ParsedIpv4(text), std::nullopt);
}
TEST(PeerGone, IsANotConnectedResetOrAbortedPeer) {
  for (auto const gone : { std::errc::not_connected, std::errc::connection_reset, std::errc::connection_aborted })
    EXPECT_TRUE(PeerGone(std::make_error_code(gone)));
  EXPECT_FALSE(PeerGone(std::make_error_code(std::errc::timed_out)));
}
TEST(SocketDeathTest, FreeRdpsNoSocketIsNoSocketToOwn) {
  EXPECT_DEATH((Socket{ NativeOf(-1), SocketLibrary{ } }), "an owned socket is open");
  EXPECT_DEATH(std::ignore = DescriptorOf(NativeOf(-1)), "the socket is open");
}
TEST(HostName, IsNotEmpty) {
  EXPECT_FALSE(HostName().empty());
}
}
