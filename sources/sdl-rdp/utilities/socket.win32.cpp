#include <sdl-rdp/utilities/socket.hpp>

#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <oxbox/utilities/span.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace sdl_rdp::utilities::detail::socket {
namespace {
constexpr Ipv4Address Loopback{ 127, 0, 0, 1 };
auto Handle(NativeSocket socket) noexcept -> SOCKET {
  return std::to_underlying(socket);
}
auto Checked(int result, std::string_view operation) -> int {
  if (result == SOCKET_ERROR) throw std::system_error{ LastSocketError(), std::string(operation) };
  return result;
}
auto Adopted(SOCKET handle, SocketLibrary const& library, std::string_view operation) -> Socket {
  if (handle == INVALID_SOCKET) throw std::system_error{ LastSocketError(), std::string(operation) };
  return Socket{ NativeSocket{ handle }, library };
}
// SO_RCVTIMEO and SO_SNDTIMEO take effect only on a socket WSASocketW created with the overlapped attribute.
auto OpenedSocket() -> Socket {
  SocketLibrary const library;
  constexpr auto      flags   = WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT;
  return Adopted(::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, flags), library, "Socket creation");
}
auto Generic(sockaddr_in& address) noexcept -> sockaddr& {
  // Winsock calls take an IPv4 address through the generic sockaddr it begins with.
  return reinterpret_cast<sockaddr&>(address);
}
auto Address(Ipv4Endpoint const& endpoint) -> sockaddr_in {
  sockaddr_in address{ };
  address.sin_family = AF_INET;
  address.sin_port   = ::htons(endpoint.port);
  address.sin_addr   = std::bit_cast<in_addr>(endpoint.address);
  return address;
}
auto Option(SOCKET handle, int option, std::uint32_t value, std::string_view operation) -> void {
  auto const bytes = oxbox::utilities::SpanCast<char const>(oxbox::utilities::AsBytes(value));
  Checked(::setsockopt(handle, SOL_SOCKET, option, bytes.data(), Narrowed<int>(bytes.size())), operation);
}
auto Shut(Socket::Direction direction) -> int {
  switch (direction) {
  case Socket::Direction::Receive: return SD_RECEIVE;
  case Socket::Direction::Send:    return SD_SEND;
  default:                         Unreachable(direction);
  }
}
template <auto name>
auto Named(Socket const& socket, std::string_view operation) -> sockaddr_in {
  sockaddr_in address { };
  int         size    = sizeof(address);
  Checked(name(Handle(socket.Native()), &Generic(address), &size), operation);
  return address;
}
auto Same(sockaddr_in const& left, sockaddr_in const& right) noexcept -> bool {
  return left.sin_port == right.sin_port && left.sin_addr.s_addr == right.sin_addr.s_addr;
}
// send and recv may be partial, so a span past INT_MAX is sent or filled in part.
auto Partial(std::size_t size) noexcept -> int {
  return static_cast<int>(std::min(size, static_cast<std::size_t>(std::numeric_limits<int>::max())));
}
auto Accepted(Socket const& listener) -> Socket {
  SocketLibrary const library;
  return Adopted(::accept(Handle(listener.Native()), nullptr, nullptr), library, "Socket accept");
}
}
Socket::~Socket() {
  if (Owns()) ::closesocket(Handle(_native));
}
auto Socket::Port() const -> std::uint16_t {
  return ::ntohs(Named<::getsockname>(*this, "Socket name").sin_port);
}
auto Socket::LimitBlockedCalls(std::chrono::milliseconds limit) const -> void {
  Expects(limit > std::chrono::milliseconds::zero(), "a zero limit would mean none");
  // Winsock takes the limit in milliseconds where POSIX takes a timeval.
  for (auto const option : { SO_RCVTIMEO, SO_SNDTIMEO })
    Option(Handle(Native()), option, Narrowed<std::uint32_t>(limit.count()), "Socket call limit");
}
auto Socket::Shutdown(Direction direction) const noexcept -> bool {
  return ::shutdown(Handle(Native()), Shut(direction)) != SOCKET_ERROR;
}
auto Socket::Send(std::span<char const> data) const noexcept -> std::ptrdiff_t {
  return ::send(Handle(Native()), data.data(), Partial(data.size()), 0);
}
auto Socket::Receive(std::span<char> data) const noexcept -> std::ptrdiff_t {
  return ::recv(Handle(Native()), data.data(), Partial(data.size()), 0);
}
auto ConnectedSockets() -> SocketPair {
  auto const listener = ListeningSocket({ .address = Loopback, .port = 0 }, 1);
  auto       client   = OpenedSocket();
  auto       address  = Address({ .address = Loopback, .port = listener.Port() });
  Checked(::connect(Handle(client.Native()), &Generic(address), sizeof(address)), "Socket pair connect");
  auto server = Accepted(listener);
  // Any local process can reach the listener: the pair is only the connection our client made.
  if (!Same(Named<::getpeername>(server, "Socket peer name"), Named<::getsockname>(client, "Socket name")))
    throw std::system_error(std::make_error_code(std::errc::connection_refused), "Socket pair accept");
  return { .server = std::move(server), .client = std::move(client) };
}
auto ListeningSocket(Ipv4Endpoint const& endpoint, int backlog) -> Socket {
  Expects(backlog > 0, "the listener queues at least one connection");
  auto socket  = OpenedSocket();
  auto address = Address(endpoint);
  // Winsock's SO_REUSEADDR lets another socket take the port; SO_EXCLUSIVEADDRUSE refuses it.
  Option(Handle(socket.Native()), SO_EXCLUSIVEADDRUSE, 1, "Socket options");
  Checked(::bind(Handle(socket.Native()), &Generic(address), sizeof(address)), "Listener bind");
  Checked(::listen(Handle(socket.Native()), backlog), "Listener listen");
  return socket;
}
auto LastSocketError() noexcept -> std::error_code {
  return { ::WSAGetLastError(), std::system_category() };
}
auto HostName() -> std::string {
  SocketLibrary const   library;
  std::array<char, 256> name   { };
  Checked(::gethostname(name.data(), Narrowed<int>(name.size() - 1)), "Hostname");
  return name.data();
}
}
