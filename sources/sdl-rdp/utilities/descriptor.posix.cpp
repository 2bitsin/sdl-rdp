#include <sdl-rdp/utilities/descriptor.posix.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <cerrno>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace sdl_rdp::utilities::detail::descriptor {

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
}
