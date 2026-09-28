#include <sdl-rdp/auth/unsignalled-socket-bio.hpp>

#include <sdl-rdp/auth/exceptions.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/socket.hpp>

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>
#include <system_error>

namespace sdl_rdp::auth::detail::unsignalled_socket_bio {
using sdl_rdp::utilities::AllocationFailed;
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::LastSocketError;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Releases;

namespace {
using Method = std::unique_ptr<BIO_METHOD, Releases<BIO_meth_free>>;
// OpenSSL's BIO control table dictates its C `long` argument and result.
using ControlValue = decltype(BIO_ctrl(nullptr, 0, 0, nullptr));

auto SocketOf(BIO& bio) -> Socket const& {
  auto const* const socket = static_cast<Socket const*>(BIO_get_data(&bio));
  Expects(socket != nullptr, "the BIO holds its socket");
  return *socket;
}
auto Interrupted() -> bool {
  return LastSocketError() == std::errc::interrupted;
}
// OpenSSL reports a BIO's -1 or 0 to its own caller as the I/O failure; there is no diagnostics route here.
constexpr auto Unreported = [](std::string_view) noexcept { };
constexpr int  IoFailed   = -1;

auto Sent(BIO& bio, std::span<char const> data) -> int {
  BIO_clear_retry_flags(&bio);
  auto const sent = SocketOf(bio).Send(data);
  if (sent < 0 && Interrupted()) BIO_set_retry_write(&bio);
  return Narrowed<int>(sent);
}
auto Received(BIO& bio, std::span<char> data) -> int {
  BIO_clear_retry_flags(&bio);
  auto const received = SocketOf(bio).Receive(data);
  if (received < 0 && Interrupted()) BIO_set_retry_read(&bio);
  return Narrowed<int>(received);
}
auto Write(BIO* bio, char const* data, int size) noexcept -> int {
  Expects(bio != nullptr, "OpenSSL writes through its BIO");
  Expects(data != nullptr, "the data exists");
  Expects(size >= 0, "the size is not negative");
  auto const sending = std::span{ data, static_cast<std::size_t>(size) };
  return Contained(IoFailed, [&] { return Sent(*bio, sending); }, Unreported);
}
auto Read(BIO* bio, char* data, int size) noexcept -> int {
  Expects(bio != nullptr, "OpenSSL reads through its BIO");
  Expects(data != nullptr, "the buffer exists");
  Expects(size >= 0, "the size is not negative");
  auto const receiving = std::span{ data, static_cast<std::size_t>(size) };
  return Contained(IoFailed, [&] { return Received(*bio, receiving); }, Unreported);
}
auto Control(BIO* bio, int command, [[maybe_unused]] ControlValue argument, [[maybe_unused]] void* pointer) noexcept
    -> ControlValue {
  Expects(bio != nullptr, "OpenSSL controls its BIO");
  // OpenSSL sends an open set of commands; 0 answers every one a plain socket does not support.
  switch (command) {
  case BIO_CTRL_FLUSH: return 1;
  default:             return 0;
  }
}
auto NewMethod() -> Method {
  Method method{ BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "sdl-rdp unsignalled socket") };
  if (!method) throw AllocationFailed{ "Socket BIO method" };
  auto* const filling = method.get();
  if (!BIO_meth_set_write(filling, Write) || !BIO_meth_set_read(filling, Read) || !BIO_meth_set_ctrl(filling, Control))
    throw BioMethodSetupFailed{ };
  return method;
}
auto SharedMethod() -> BIO_METHOD const& {
  // A function-local static is initialised once under the C++ runtime's lock.
  static Method const method{ NewMethod() };
  return *method;
}
}
// Unlike OpenSSL's socket BIO it sends without SIGPIPE.
auto UnsignalledSocketBio(Socket& socket) -> Bio {
  Expects(socket.Owns(), "the socket is open");
  Bio bio{ BIO_new(&SharedMethod()) };
  if (!bio) throw AllocationFailed{ "Socket BIO" };
  BIO_set_data(bio.get(), &socket);
  BIO_set_init(bio.get(), 1);
  return bio;
}
}
