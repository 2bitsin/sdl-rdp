#include <sdl-rdp/utilities/socket.posix.hpp>

#include <sdl-rdp/utilities/descriptor.posix.hpp>
#include <sdl-rdp/utilities/socket.hpp>

#include <array>
#include <fcntl.h>
#include <sys/socket.h>
#include <utility>

namespace sdl_rdp::utilities::detail::socket {
namespace {
auto ClosedOnExec(Socket socket) -> Socket {
  SystemCall(::fcntl(DescriptorOf(socket.Native()), F_SETFD, FD_CLOEXEC), "Socket close on exec");
  return socket;
}
}
auto OpenedSocket(int domain, int type) -> Socket {
  SocketLibrary const library;
  return ClosedOnExec(Socket{ NativeOf(SystemCall(::socket(domain, type, 0), "Socket creation")), library });
}
auto OpenedPair(int domain, int type) -> SocketPair {
  SocketLibrary const library;
  std::array<int, 2>  ends   { };
  SystemCall(::socketpair(domain, type, 0, ends.data()), "Socket pair");
  SocketPair opened{ .server = Socket{ NativeOf(ends[0]), library }, .client = Socket{ NativeOf(ends[1]), library } };
  return { .server = ClosedOnExec(std::move(opened.server)), .client = ClosedOnExec(std::move(opened.client)) };
}
}
