#include <sdl-rdp/utilities/socket.hpp>

#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <oxbox/utilities/number-text.hpp>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace sdl_rdp::utilities::detail::socket {
namespace {
constexpr NativeSocket Closed{ ~std::uintptr_t{ 0 } };

auto Dotted(Ipv4Address const& address) -> std::string {
  return std::format("{}.{}.{}.{}", address[0], address[1], address[2], address[3]);
}
}
Socket::Socket(NativeSocket owned, SocketLibrary library) noexcept : _library{ std::move(library) }, _native{ owned } {
  Expects(owned != Closed, "an owned socket is open");
}
Socket::Socket(Socket&& other) noexcept
    : _library{ std::move(other._library) }, _native{ std::exchange(other._native, Closed) } { }
auto Socket::operator=(Socket&& other) noexcept -> Socket& {
  Socket released{ std::move(other) };
  std::swap(_native, released._native);
  return *this;
}
auto Socket::Owns() const noexcept -> bool {
  return _native != Closed;
}
auto Socket::Native() const noexcept -> NativeSocket {
  Expects(Owns(), "the socket is still owned");
  return _native;
}
auto Socket::Release() noexcept -> NativeSocket {
  Expects(Owns(), "the socket is still owned");
  return std::exchange(_native, Closed);
}
auto ParsedIpv4(std::string_view text) -> std::optional<Ipv4Address> {
  // Only the canonical spelling: ParseNumbers reads radix markers, and inet_pton refuses a leading zero.
  auto const parsed = oxbox::utilities::ParseNumbers<std::uint8_t, 4>(text, '.');
  if (!parsed || Dotted(*parsed) != text) return std::nullopt;
  return parsed;
}
auto PeerGone(std::error_code error) noexcept -> bool {
  auto const condition = error.default_error_condition();
  if (condition.category() != std::generic_category()) return false;
  switch (static_cast<std::errc>(condition.value())) {
  case std::errc::not_connected:
  case std::errc::connection_reset:
  case std::errc::connection_aborted: return true;
  default:                            return false;
  }
}
auto DescriptorOf(NativeSocket socket) -> int {
  Expects(socket != Closed, "the socket is open");
  return Narrowed<int>(std::to_underlying(socket));
}
auto NativeOf(int descriptor) noexcept -> NativeSocket {
  return NativeSocket{ static_cast<std::uintptr_t>(descriptor) };
}
}
