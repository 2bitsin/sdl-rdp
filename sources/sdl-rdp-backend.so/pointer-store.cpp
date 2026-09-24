#include "_detail/pointer-store.hpp"

#include <utility>

namespace Backend {
void PointerStore::Replace(PointerShape next) {
  _shape = std::move(next);
  ++_generation;
}
uint64_t PointerStore::Generation() const noexcept {
  return _generation;
}
PointerDelivery PointerStore::Send(rdpContext& context) {
  return _shape.Send(context);
}
}
