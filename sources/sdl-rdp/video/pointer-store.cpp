#include <sdl-rdp/video/pointer-store.hpp>

namespace Backend {
auto PointerStore::Send(rdpContext& context) const -> PointerDelivery {
  return Value().Send(context);
}
}
