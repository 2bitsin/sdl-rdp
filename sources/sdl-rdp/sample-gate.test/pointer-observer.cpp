#include <sdl-rdp/sample-gate.test/pointer-observer.hpp>

#include <algorithm>
#include <cstdint>

namespace SampleGate {
PointerObserver::PointerObserver(Headless::Client& client) {
  active = this;
  // abi: pPointerNew, BOOL is int
  client.Instance()->context->update->pointer->PointerNew = [](rdpContext*, POINTER_NEW_UPDATE const* update) -> int {
    active->Receive(*update);
    return true;
  };
}
auto PointerObserver::Red() const -> bool {
  return red;
}
auto PointerObserver::Receive(POINTER_NEW_UPDATE const& update) -> void {
  auto const& shape = update.colorPtrAttr;
  if (shape.width != 8 || shape.height != 8 || update.xorBpp != 32) return;
  auto const* pixels = reinterpret_cast<std::uint32_t const*>(shape.xorMaskData);
  red = std::all_of(pixels, pixels + 64, [](std::uint32_t pixel) { return pixel == 0xffff0000; });
}
}
