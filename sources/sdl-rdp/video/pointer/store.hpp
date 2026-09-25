#pragma once
#include <sdl-rdp/utilities/generational.hpp>
#include <sdl-rdp/video/pointer/shape.hpp>

namespace sdl_rdp::video::pointer::detail::store {
using sdl_rdp::utilities::Generational;

class PointerStore : private Generational<PointerShape> {
public:
  using Generational::Generation;
  using Generational::Replace;
  auto Send(rdpContext& context) const -> PointerDelivery;
};
}

namespace sdl_rdp::video::pointer {
using detail::store::PointerStore;
}
