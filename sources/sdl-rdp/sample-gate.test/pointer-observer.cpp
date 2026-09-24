#include <sdl-rdp/sample-gate.test/pointer-observer.hpp>

#include <algorithm>

namespace SampleGate {
PointerObserver::PointerObserver(Headless::Client& client) {
  active                                                  = this;
  client.Instance()->context->update->pointer->PointerNew = Receive;
}
auto PointerObserver::Red() const -> bool {
  return red;
}
auto PointerObserver::Receive(rdpContext* /*unused*/, POINTER_NEW_UPDATE const* update) -> BOOL {
  auto const& shape = update->colorPtrAttr;
  if (shape.width != 8 || shape.height != 8 || update->xorBpp != 32) return TRUE;
  auto const* pixels = reinterpret_cast<UINT32 const*>(shape.xorMaskData);
  active->red = std::all_of(pixels, pixels + 64, [](UINT32 pixel) { return pixel == 0xffff0000; });
  return TRUE;
}
}
