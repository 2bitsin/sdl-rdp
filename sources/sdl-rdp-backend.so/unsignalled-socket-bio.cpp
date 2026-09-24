#include "_detail/unsignalled-socket-bio.hpp"

#include "_detail/contract.hpp"

#include <cerrno>
#include <cstddef>
#include <memory>
#include <new>
#include <stdexcept>
#include <sys/socket.h>
#include <tuple>

namespace Backend {
namespace {
using utilities::Expects;
using Method = std::unique_ptr<BIO_METHOD, Releases<BIO_meth_free>>;
constexpr int Unset = -1;

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
auto Write(BIO* bio, char const* data, int size) noexcept -> int {
  Expects(data != nullptr, "the data exists");
  Expects(size >= 0, "the size is not negative");
  BIO_clear_retry_flags(bio);
  auto const sent = ::send(SocketOf(bio), data, static_cast<std::size_t>(size), MSG_NOSIGNAL);
  if (sent < 0 && errno == EINTR) BIO_set_retry_write(bio);
  return static_cast<int>(sent);
}
auto Read(BIO* bio, char* data, int size) noexcept -> int {
  Expects(data != nullptr, "the buffer exists");
  Expects(size >= 0, "the size is not negative");
  BIO_clear_retry_flags(bio);
  auto const received = ::recv(SocketOf(bio), data, static_cast<std::size_t>(size), 0);
  if (received < 0 && errno == EINTR) BIO_set_retry_read(bio);
  return static_cast<int>(received);
}
auto Adopt(BIO* bio, int socket, long closing) -> long {
  Expects(socket >= 0, "the socket is open");
  Expects(closing == BIO_NOCLOSE, "the caller closes the socket");
  Slot(bio) = socket;
  BIO_set_init(bio, 1);
  return 1;
}
auto Reported(BIO* bio, void const* out) -> long {
  Expects(out == nullptr, "the socket is read from the result");
  return Slot(bio);
}
auto Control(BIO* bio, int command, long argument, void* pointer) noexcept -> long {
  // OpenSSL sends an open set of commands; 0 answers every one a plain socket does not support.
  switch (command) {
  case BIO_C_SET_FD:
    return Adopt(bio, *static_cast<int const*>(pointer), argument);
  case BIO_C_GET_FD:
    return Reported(bio, pointer);
  case BIO_CTRL_FLUSH:
    return 1;
  default:
    return 0;
  }
}
auto Create(BIO* bio) noexcept -> int {
  try {
    BIO_set_data(bio, std::make_unique<int>(Unset).release());
    return 1;
  } catch (std::bad_alloc const&) {
    return 0;
  }
}
auto Destroy(BIO* bio) noexcept -> int {
  std::unique_ptr<int> const slot{ static_cast<int*>(BIO_get_data(bio)) };
  BIO_set_data(bio, nullptr);
  return 1;
}
auto NewMethod() -> Method {
  Method method{ BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "sdl-rdp unsignalled socket") };
  if (!method) throw std::runtime_error("Socket BIO method allocation failed.");
  auto* const filling = method.get();
  if (!BIO_meth_set_write(filling, Write) || !BIO_meth_set_read(filling, Read) ||
      !BIO_meth_set_ctrl(filling, Control) || !BIO_meth_set_create(filling, Create) ||
      !BIO_meth_set_destroy(filling, Destroy))
    throw std::runtime_error("Socket BIO method setup failed.");
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
  if (!bio) throw std::runtime_error("Socket BIO allocation failed.");
  std::ignore = BIO_set_fd(bio.get(), socket, BIO_NOCLOSE);
  return bio;
}
}
