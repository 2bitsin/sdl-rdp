#pragma once
#include "pointer-shape.hpp"

#include <cstdint>

namespace Backend {
class PointerStore {
public:
  auto Replace(PointerShape next)  -> void;
  auto Generation() const noexcept -> uint64_t;
  auto Send(rdpContext& context)   -> PointerDelivery;

private:
  PointerShape _shape;
  uint64_t     _generation{ };
};
}
