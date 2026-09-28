#include <sdl-rdp/utilities/socket.posix.hpp>

#include <sdl-rdp/utilities/descriptor.posix.hpp>
#include <sdl-rdp/utilities/socket.hpp>

#include <array>
#include <sys/socket.h>

namespace sdl_rdp::utilities::detail::socket {
auto OpenedSocket(int domain, int type) -> Socket {
  SocketLibrary const library;
  return Socket{ NativeOf(SystemCall(::socket(domain, type | SOCK_CLOEXEC, 0), "Socket creation")), library };
}
auto OpenedPair(int domain, int type) -> SocketPair {
  SocketLibrary const library;
  std::array<int, 2>  ends   { };
  SystemCall(::socketpair(domain, type | SOCK_CLOEXEC, 0, ends.data()), "Socket pair");
  return { .server = Socket{ NativeOf(ends[0]), library }, .client = Socket{ NativeOf(ends[1]), library } };
}
}
