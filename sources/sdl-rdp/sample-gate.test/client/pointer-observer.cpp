#include <sdl-rdp/sample-gate.test/client/pointer-observer.hpp>

#include <sdl-rdp/headless-client.test/client/handles.hpp>
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/sample-gate.test/client/pointer-updates.hpp>

#include <oxbox/utilities/span.hpp>
#include <algorithm>
#include <cstdint>
#include <span>

namespace sdl_rdp::sample_gate_test::client::detail::pointer_observer {
using oxbox::utilities::SpanCast;
using sdl_rdp::headless_client_test::client::ClientContext;
using sdl_rdp::headless_client_test::utilities::ObserverSet;
using sdl_rdp::utilities::Expects;

PointerObserver::PointerObserver(Client& client)
    : context(ClientContext(client)), pointer(PointerUpdates(client)), original(pointer.PointerNew),
      membership(context, *this) {
  // abi: pPointerNew, BOOL is int
  pointer.PointerNew = [](rdpContext* context, POINTER_NEW_UPDATE const* update) -> int {
    Expects(context != nullptr, "the pointer shape names its client context");
    Expects(update != nullptr, "the new pointer shape is supplied");
    ObserverSet::Of(*context).Held<PointerObserver>()->Receive(*update);
    return true;
  };
}
PointerObserver::~PointerObserver() {
  pointer.PointerNew = original;
}
auto PointerObserver::Red() const -> bool {
  return red;
}
auto PointerObserver::Receive(POINTER_NEW_UPDATE const& update) -> void {
  auto const& shape = update.colorPtrAttr;
  if (shape.width != 8 || shape.height != 8 || update.xorBpp != 32) return;
  auto const pixels = SpanCast<std::uint32_t const>(std::span(shape.xorMaskData, shape.lengthXorMask));
  if (pixels.size() != 64) return;
  red = std::ranges::all_of(pixels, [](std::uint32_t pixel) { return pixel == 0xffff0000; });
}
}
