#pragma once
#include <sdl-rdp/utilities/socket-library.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace sdl_rdp::utilities::detail::socket {
using sdl_rdp::utilities::SocketLibrary;

// An int on POSIX, a SOCKET on Windows; all ones is no socket on both.
enum class NativeSocket : std::uintptr_t { };
using Ipv4Address = std::array<std::uint8_t, 4>;
struct Ipv4Endpoint {
  Ipv4Address   address;
  std::uint16_t port;
};
// An owned stream socket, closed by its destructor.
class Socket {
public:
  enum class Direction : std::uint8_t { Receive, Send };
  // The creator's reference on the library, taken before it opened the socket.
                     Socket(NativeSocket owned, SocketLibrary library)                   noexcept;
                     Socket(Socket&& other)                                              noexcept;
                     Socket(Socket const&)                                               = delete;
                     ~Socket();
  auto               operator=(Socket&& other) noexcept                       -> Socket&;
  auto               operator=(Socket const&)                                 -> Socket& = delete;
  [[nodiscard]] auto Owns() const noexcept                                    -> bool;
  [[nodiscard]] auto Native() const noexcept                                  -> NativeSocket;
  [[nodiscard]] auto Release() noexcept                                       -> NativeSocket;
  [[nodiscard]] auto Port() const                                             -> std::uint16_t;
  auto               LimitBlockedCalls(std::chrono::milliseconds limit) const -> void;
  [[nodiscard]] auto Shutdown(Direction direction) const noexcept             -> bool;
  // char, not std::byte: the one caller is an OpenSSL BIO, whose buffers are char.
  [[nodiscard]] auto Send(std::span<char const> data) const noexcept -> std::ptrdiff_t;
  [[nodiscard]] auto Receive(std::span<char> data) const noexcept    -> std::ptrdiff_t;

private:
  SocketLibrary _library;
  NativeSocket  _native;
};
struct SocketPair {
  Socket server;
  Socket client;
};
auto ConnectedSockets()                                         -> SocketPair;
auto ListeningSocket(Ipv4Endpoint const& endpoint, int backlog) -> Socket;
auto ParsedIpv4(std::string_view text)                          -> std::optional<Ipv4Address>;
auto LastSocketError() noexcept                                 -> std::error_code;
auto PeerGone(std::error_code error) noexcept                   -> bool;
auto HostName()                                                 -> std::string;
// FreeRDP takes a socket as an int on every platform, a SOCKET narrowed on Windows.
auto DescriptorOf(NativeSocket socket) -> int;
auto NativeOf(int descriptor) noexcept -> NativeSocket;
}

namespace sdl_rdp::utilities {
using detail::socket::ConnectedSockets;
using detail::socket::DescriptorOf;
using detail::socket::HostName;
using detail::socket::Ipv4Address;
using detail::socket::Ipv4Endpoint;
using detail::socket::LastSocketError;
using detail::socket::ListeningSocket;
using detail::socket::NativeOf;
using detail::socket::NativeSocket;
using detail::socket::ParsedIpv4;
using detail::socket::PeerGone;
using detail::socket::Socket;
using detail::socket::SocketPair;
}
