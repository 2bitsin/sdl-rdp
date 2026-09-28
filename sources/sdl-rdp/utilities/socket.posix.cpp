#include <sdl-rdp/utilities/socket.hpp>

#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/descriptor.posix.hpp>
#include <sdl-rdp/utilities/socket.posix.hpp>

#include <arpa/inet.h>
#include <array>
#include <bit>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

namespace sdl_rdp::utilities::detail::socket {
namespace {
auto Generic(sockaddr_in& address) noexcept -> sockaddr& {
  // POSIX socket calls take an IPv4 address through the generic sockaddr it begins with.
  return reinterpret_cast<sockaddr&>(address);
}
auto Address(Ipv4Endpoint const& endpoint) -> sockaddr_in {
  sockaddr_in address{ };
  address.sin_family = AF_INET;
  address.sin_port   = htons(endpoint.port);
  address.sin_addr   = std::bit_cast<in_addr>(endpoint.address);
  return address;
}
auto Timeval(std::chrono::microseconds span) -> timeval {
  auto const seconds = std::chrono::floor<std::chrono::seconds>(span);
  auto const rest    = span - seconds;
  return { .tv_sec = static_cast<time_t>(seconds.count()), .tv_usec = static_cast<suseconds_t>(rest.count()) };
}
auto Shut(Socket::Direction direction) -> int {
  switch (direction) {
  case Socket::Direction::Receive: return SHUT_RD;
  case Socket::Direction::Send:    return SHUT_WR;
  default:                         Unreachable(direction);
  }
}
}
Socket::~Socket() {
  if (Owns()) ::close(DescriptorOf(_native));
}
auto Socket::Port() const -> std::uint16_t {
  sockaddr_in address { };
  socklen_t   size    = sizeof(address);
  SystemCall(::getsockname(DescriptorOf(Native()), &Generic(address), &size), "Socket name");
  return ntohs(address.sin_port);
}
auto Socket::LimitBlockedCalls(std::chrono::milliseconds limit) const -> void {
  Expects(limit > std::chrono::milliseconds::zero(), "a zero limit would mean none");
  auto const bound = Timeval(limit);
  for (auto const option : { SO_RCVTIMEO, SO_SNDTIMEO })
    SystemCall(::setsockopt(DescriptorOf(Native()), SOL_SOCKET, option, &bound, sizeof(bound)), "Socket call limit");
}
auto Socket::Shutdown(Direction direction) const noexcept -> bool {
  return ::shutdown(DescriptorOf(Native()), Shut(direction)) == 0;
}
auto Socket::Send(std::span<char const> data) const noexcept -> std::ptrdiff_t {
  return ::send(DescriptorOf(Native()), data.data(), data.size(), MSG_NOSIGNAL);
}
auto Socket::Receive(std::span<char> data) const noexcept -> std::ptrdiff_t {
  return ::recv(DescriptorOf(Native()), data.data(), data.size(), 0);
}
auto ConnectedSockets() -> SocketPair {
  return OpenedPair(AF_UNIX, SOCK_STREAM);
}
auto ListeningSocket(Ipv4Endpoint const& endpoint, int backlog) -> Socket {
  Expects(backlog > 0, "the listener queues at least one connection");
  auto       socket  = OpenedSocket(AF_INET, SOCK_STREAM);
  auto const handle  = DescriptorOf(socket.Native());
  auto       address = Address(endpoint);
  int const  reuse   = 1;
  // POSIX SO_REUSEADDR binds past TIME_WAIT and still refuses a second listener: Windows' SO_EXCLUSIVEADDRUSE.
  SystemCall(::setsockopt(handle, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)), "Socket options");
  SystemCall(::bind(handle, &Generic(address), sizeof(address)), "Listener bind");
  SystemCall(::listen(handle, backlog), "Listener listen");
  return socket;
}
auto LastSocketError() noexcept -> std::error_code {
  return { errno, std::system_category() };
}
auto HostName() -> std::string {
  std::array<char, 256> name{ };
  SystemCall(::gethostname(name.data(), name.size() - 1), "Hostname");
  return name.data();
}
}
