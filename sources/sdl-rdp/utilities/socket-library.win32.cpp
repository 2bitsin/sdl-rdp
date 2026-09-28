#include <sdl-rdp/utilities/socket-library.hpp>

#include <winsock2.h>

namespace sdl_rdp::utilities::detail::socket_library {
namespace {
constexpr auto WinsockVersion = MAKEWORD(2, 2);
}
// WSAStartup and WSACleanup are counted, so the count reaches zero when the last owner of a socket ends.
auto SocketLibrary::Start() noexcept -> int {
  WSADATA data{ };
  return ::WSAStartup(WinsockVersion, &data);
}
auto SocketLibrary::Stop() noexcept -> void {
  ::WSACleanup();
}
}
