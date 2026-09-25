#include "_detail/unsignalled-socket-bio.hpp"

#include <sdl-rdp/auth/exceptions.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>

#include <cerrno>
#include <cstddef>
#include <memory>
#include <string_view>
#include <sys/socket.h>
#include <tuple>

namespace Backend {
namespace {
using utilities::Expects;
using Method = std::unique_ptr<BIO_METHOD, Releases<BIO_meth_free>>;
constexpr int Unset = -1;
// OpenSSL's BIO control table dictates its C `long` argument and result.
using ControlValue = decltype(BIO_ctrl(nullptr, 0, 0, nullptr));

auto Slot(BIO* bio) -> int& {
  Expects(bio != nullptr, "the BIO exists");
  auto* const slot = static_cast<int*>(BIO_get_data(bio));
  Expects(slot != nullptr, "the BIO holds its socket slot");
  return *slot;
}
auto SocketOf(BIO* bio) -> int {
  auto const socket = static_cast<int>(BIO_get_fd(bio, nullptr));
  Expects(socket >= 0, "the BIO has its socket");
  return socket;
}
// OpenSSL reports a BIO's -1 or 0 to its own caller as the I/O failure; there is no diagnostics route here.
constexpr auto Unreported = [](std::string_view) noexcept { };
constexpr int  IoFailed   = -1;

auto Sent(BIO* bio, char const* data, int size) -> int {
  Expects(data != nullptr, "the data exists");
  Expects(size >= 0, "the size is not negative");
  BIO_clear_retry_flags(bio);
  auto const sent = ::send(SocketOf(bio), data, static_cast<std::size_t>(size), MSG_NOSIGNAL);
  if (sent < 0 && errno == EINTR) BIO_set_retry_write(bio);
  return static_cast<int>(sent);
}
auto Received(BIO* bio, char* data, int size) -> int {
  Expects(data != nullptr, "the buffer exists");
  Expects(size >= 0, "the size is not negative");
  BIO_clear_retry_flags(bio);
  auto const received = ::recv(SocketOf(bio), data, static_cast<std::size_t>(size), 0);
  if (received < 0 && errno == EINTR) BIO_set_retry_read(bio);
  return static_cast<int>(received);
}
auto Write(BIO* bio, char const* data, int size) noexcept -> int {
  return Contained(IoFailed, [&] { return Sent(bio, data, size); }, Unreported);
}
auto Read(BIO* bio, char* data, int size) noexcept -> int {
  return Contained(IoFailed, [&] { return Received(bio, data, size); }, Unreported);
}
auto Adopt(BIO* bio, int socket, ControlValue closing) -> ControlValue {
  Expects(socket >= 0, "the socket is open");
  Expects(closing == BIO_NOCLOSE, "the caller closes the socket");
  Slot(bio) = socket;
  BIO_set_init(bio, 1);
  return 1;
}
auto Reported(BIO* bio, void const* out) -> ControlValue {
  Expects(out == nullptr, "the socket is read from the result");
  return Slot(bio);
}
auto Control(BIO* bio, int command, ControlValue argument, void* pointer) noexcept -> ControlValue {
  // OpenSSL sends an open set of commands; 0 answers every one a plain socket does not support.
  switch (command) {
  case BIO_C_SET_FD:   return Adopt(bio, *static_cast<int const*>(pointer), argument);
  case BIO_C_GET_FD:   return Reported(bio, pointer);
  case BIO_CTRL_FLUSH: return 1;
  default:             return 0;
  }
}
auto Create(BIO* bio) noexcept -> int {
  auto const created = [&] {
    BIO_set_data(bio, std::make_unique<int>(Unset).release());
    return 1;
  };
  return Contained(0, created, Unreported);
}
auto Destroy(BIO* bio) noexcept -> int {
  auto const released = [bio] {
    std::unique_ptr<int> const slot{ static_cast<int*>(BIO_get_data(bio)) };
    BIO_set_data(bio, nullptr);
    return 1;
  };
  return Contained(0, released, Unreported);
}
auto NewMethod() -> Method {
  Method method{ BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "sdl-rdp unsignalled socket") };
  if (!method) throw AllocationFailed{ "Socket BIO method" };
  auto* const filling = method.get();
  if (!BIO_meth_set_write(filling, Write) || !BIO_meth_set_read(filling, Read) || !BIO_meth_set_ctrl(filling, Control)
      || !BIO_meth_set_create(filling, Create) || !BIO_meth_set_destroy(filling, Destroy))
    throw BioMethodSetupFailed{ };
  return method;
}
auto SharedMethod() -> BIO_METHOD const* {
  // A function-local static is initialised once under the C++ runtime's lock.
  static Method const method{ NewMethod() };
  return method.get();
}
}
// Unlike OpenSSL's socket BIO it sends with MSG_NOSIGNAL; the caller keeps the socket open while the BIO lives.
auto UnsignalledSocketBio(int socket) -> Bio {
  Expects(socket >= 0, "the socket is open");
  Bio bio{ BIO_new(SharedMethod()) };
  if (!bio) throw AllocationFailed{ "Socket BIO" };
  std::ignore = BIO_set_fd(bio.get(), socket, BIO_NOCLOSE);
  return bio;
}
}
