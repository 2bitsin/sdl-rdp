#include <sdl-rdp/utilities/socket-pair.hpp>

#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/system-call.hpp>

#include <array>
#include <sys/socket.h>

namespace Backend {
namespace {
auto Connected() -> std::pair<Descriptor, Descriptor> {
  std::array<int, 2> ends{ };
  SystemCall(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ends.data()), "socket pair");
  return { Descriptor{ ends[0] }, Descriptor{ ends[1] } };
}
}
SocketPair::SocketPair() : SocketPair(Connected()) { }
SocketPair::SocketPair(std::pair<Descriptor, Descriptor> connected)
    : server(std::move(connected.first)), client(std::move(connected.second)) {
  utilities::Expects(server.Owns(), "the server end is open");
  utilities::Expects(client.Owns(), "the client end is open");
}
auto SocketPair::TakeServer() -> Descriptor {
  utilities::Expects(server.Owns(), "the server end is taken once");
  return std::move(server);
}
auto SocketPair::Client() const noexcept -> int {
  return client.Get();
}
}
