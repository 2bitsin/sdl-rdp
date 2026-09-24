#pragma once
#include <sdl-rdp/utilities/generational.hpp>
#include <sdl-rdp/video/pointer-shape.hpp>

namespace Backend {
class PointerStore : private Generational<PointerShape> {
public:
  using Generational::Generation;
  using Generational::Replace;
  auto Send(rdpContext& context) const -> PointerDelivery;
};
}
