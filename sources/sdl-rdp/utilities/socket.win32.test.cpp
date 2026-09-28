#include <sdl-rdp/utilities/socket.hpp>

#include <gtest/gtest.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <string_view>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>

namespace sdl_rdp::utilities::detail::socket {
namespace {
constexpr Ipv4Address      Loopback{ 127, 0, 0, 1 };
constexpr std::string_view Greeting{ "hello"      };
// The first send after the peer closed is buffered; the peer's reset fails a later one.
constexpr int SendAttempts = 100;

auto FirstFailedSend(Socket const& socket) -> std::error_code {
  for (int attempt = 0; attempt < SendAttempts; ++attempt) {
    if (socket.Send(Greeting) < 0) return LastSocketError();
    std::this_thread::sleep_for(std::chrono::milliseconds{ 1 });
  }
  return { };
}
}
TEST(ConnectedSockets, AreTheTwoEndsOfOneLoopbackConnection) {
  auto const pair = ConnectedSockets();
  EXPECT_NE(pair.server.Port(), pair.client.Port());
  EXPECT_EQ(pair.client.Send(Greeting), std::ssize(Greeting));
  std::array<char, 16> buffer{ };
  EXPECT_EQ(pair.server.Receive(buffer), std::ssize(Greeting));
}
TEST(ListeningSocket, RefusesASecondListenerOnItsPortExclusively) {
  auto const listener = ListeningSocket({ .address = Loopback, .port = 0 }, 1);
  try {
    std::ignore = ListeningSocket({ .address = Loopback, .port = listener.Port() }, 1);
    ADD_FAILURE() << "a second listener bound the port";
  } catch (std::system_error const& refused) {
    EXPECT_EQ(refused.code(), std::errc::address_in_use) << refused.what();
  }
}
TEST(Socket, SendToAClosedPeerFailsWithoutEndingTheProcess) {
  auto pair = ConnectedSockets();
  {
    Socket const closing{ std::move(pair.server) };
  }
  auto const failed = FirstFailedSend(pair.client);
  EXPECT_TRUE(failed == std::errc::connection_reset || failed == std::errc::connection_aborted) << failed.message();
  EXPECT_TRUE(PeerGone(failed));
}
TEST(Socket, ShutdownAfterThePeerIsGoneFailsOnlyAsAGonePeer) {
  auto pair = ConnectedSockets();
  {
    Socket const closing{ std::move(pair.server) };
  }
  std::ignore = FirstFailedSend(pair.client);
  if (!pair.client.Shutdown(Socket::Direction::Receive)) EXPECT_TRUE(PeerGone(LastSocketError()));
}
}
