#include <sdl-rdp/utilities/socket.hpp>

#include <gtest/gtest.h>
#include <fcntl.h>
#include <string_view>
#include <system_error>
#include <utility>

namespace sdl_rdp::utilities::detail::socket {
namespace {
constexpr std::string_view Greeting{ "hello" };

auto ClosedOnExec(Socket const& socket) -> bool {
  return (::fcntl(DescriptorOf(socket.Native()), F_GETFD) & FD_CLOEXEC) != 0;
}
}
TEST(Socket, SendToAClosedPeerFailsWithoutEndingTheProcess) {
  auto pair = ConnectedSockets();
  {
    Socket const closing{ std::move(pair.server) };
  }
  EXPECT_EQ(pair.client.Send(Greeting), -1);
  EXPECT_EQ(LastSocketError(), std::errc::broken_pipe);
}
TEST(Socket, EveryOpenedSocketIsClosedOnExec) {
  auto const pair     = ConnectedSockets();
  auto const listener = ListeningSocket({ .address = { 127, 0, 0, 1 }, .port = 0 }, 1);
  EXPECT_TRUE(ClosedOnExec(pair.server));
  EXPECT_TRUE(ClosedOnExec(pair.client));
  EXPECT_TRUE(ClosedOnExec(listener));
}
}
