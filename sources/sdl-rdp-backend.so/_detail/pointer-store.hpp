#pragma once
#include "pointer-shape.hpp"

#include <cstdint>

namespace Backend {
class PointerStore {
public:
  void            Replace(PointerShape next);
  uint64_t        Generation() const noexcept;
  PointerDelivery Send(rdpContext& context);

private:
  PointerShape _shape;
  uint64_t     _generation{ };
};
}
