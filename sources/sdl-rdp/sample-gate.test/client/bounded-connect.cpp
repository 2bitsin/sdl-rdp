#include <sdl-rdp/sample-gate.test/client/bounded-connect.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/posix.hpp>
#include <arpa/inet.h>
#include <cstdint>
#include <netinet/in.h>
#include <string_view>
#include <sys/socket.h>

namespace sdl_rdp::sample_gate_test::client::detail::bounded_connect {
using sdl_rdp::utilities::Descriptor;
using sdl_rdp::utilities::Expects;

// abi: pTCPConnect, the transport IO table's connect slot
auto BoundedConnect([[maybe_unused]] rdpContext* context, [[maybe_unused]] rdpSettings* settings, char const* hostname,
                    int port, [[maybe_unused]] std::uint32_t timeout) -> int {
  Expects(hostname != nullptr, "the client names its host");
  Expects(std::string_view(hostname) == "127.0.0.1", "local test listener");
  Expects(port > 0, "listener has a port");
  Descriptor socket{ ::socket(AF_INET, SOCK_STREAM, 0) };
  // Linux tcp(7): SO_RCVBUF must precede connect to constrain the receive window.
  int const bytes   = 1024 * 1024;
  auto      bounded = setsockopt(socket.Get(), SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes));
  Expects(bounded == 0, "receive buffer bounded before connect");
  sockaddr_in address{ };
  address.sin_family      = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port        = htons(port);
  if (connect(socket.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) return -1;
  return socket.Release();
}
}
