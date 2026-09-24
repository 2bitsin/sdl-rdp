#include "_detail/descriptor.hpp"

#include "_detail/contract.hpp"

#include <unistd.h>
#include <utility>

namespace Backend {
using utilities::Expects;

Descriptor::Descriptor(int owned) noexcept : descriptor(owned) {
  Expects(owned >= 0, "an owned descriptor is open");
}
Descriptor::Descriptor(Descriptor&& other) noexcept : descriptor(std::exchange(other.descriptor, Closed)) { }
Descriptor::~Descriptor() {
  if (Owns()) ::close(descriptor);
}
auto Descriptor::operator = (Descriptor&& other) noexcept -> Descriptor& {
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
}
