#include "_detail/bounded-connect.hpp"
#include <sdl-rdp-backend.so/_detail/contract.hpp>
#include <sdl-rdp-backend.so/_detail/descriptor.hpp>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <string_view>
#include <sys/socket.h>

namespace SampleGate {
auto BoundedConnect([[maybe_unused]] rdpContext* context, [[maybe_unused]] rdpSettings* settings,
                    char const* hostname, int port, [[maybe_unused]] DWORD timeout) -> int {
  utilities::Expects(std::string_view(hostname) == "127.0.0.1", "local test listener");
  utilities::Expects(port > 0, "listener has a port");
  Backend::Descriptor socket  { ::socket(AF_INET, SOCK_STREAM, 0) };
  // Linux tcp(7): SO_RCVBUF must precede connect to constrain the receive window.
  int const           bytes   = 1024 * 1024;
  auto                bounded = setsockopt(socket.Get(), SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes));
  utilities::Expects(bounded == 0, "receive buffer bounded before connect");
  sockaddr_in address{ };
  address.sin_family      = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port        = htons(port);
  if (connect(socket.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) return -1;
  return socket.Release();
}
}
