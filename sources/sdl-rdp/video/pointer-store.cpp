#include <sdl-rdp/video/pointer-store.hpp>

#include <utility>

namespace Backend {
auto PointerStore::Replace(PointerShape next) -> void {
  _shape = std::move(next);
  ++_generation;
}
auto PointerStore::Generation() const noexcept -> uint64_t {
  return _generation;
}
auto PointerStore::Send(rdpContext& context) -> PointerDelivery {
  return _shape.Send(context);
}
}
