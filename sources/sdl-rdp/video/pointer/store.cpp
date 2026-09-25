#include <sdl-rdp/video/pointer/store.hpp>

namespace sdl_rdp::video::pointer::detail::store {
auto PointerStore::Send(rdpContext& context) const -> PointerDelivery {
  return Value().Send(context);
}
}
