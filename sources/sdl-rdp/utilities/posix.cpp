#include <sdl-rdp/utilities/posix.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <array>
#include <cerrno>
#include <string>
#include <sys/socket.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace sdl_rdp::utilities::detail::posix {

Descriptor::Descriptor(int owned) noexcept : descriptor(owned) {
  Expects(owned >= 0, "an owned descriptor is open");
}
Descriptor::Descriptor(Descriptor&& other) noexcept : descriptor(std::exchange(other.descriptor, Closed)) { }
Descriptor::~Descriptor() {
  if (Owns()) ::close(descriptor);
}
auto Descriptor::operator=(Descriptor&& other) noexcept -> Descriptor& {
  Descriptor released(std::move(other));
  std::swap(descriptor, released.descriptor);
  return *this;
}
auto Descriptor::Owns() const noexcept -> bool {
  return descriptor != Closed;
}
auto Descriptor::Get() const noexcept -> int {
  Expects(Owns(), "the descriptor is still owned");
  return descriptor;
}
auto Descriptor::Release() noexcept -> int {
  Expects(Owns(), "the descriptor is still owned");
  return std::exchange(descriptor, Closed);
}
auto SystemCall(int result, std::string_view operation) -> int {
  Expects(!operation.empty(), "the failing operation can be named");
  if (result < 0) throw std::system_error(errno, std::system_category(), std::string(operation));
  return result;
}
auto ConnectedSockets() -> SocketPair {
  std::array<int, 2> ends{ };
  SystemCall(::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ends.data()), "socket pair");
  return { .server = Descriptor{ ends[0] }, .client = Descriptor{ ends[1] } };
}
}
