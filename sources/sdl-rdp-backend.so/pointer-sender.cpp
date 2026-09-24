#include "_detail/pointer-sender.hpp"

#include "_detail/contract.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/peer-link.hpp"
#include "_detail/pointer-store.hpp"

namespace Backend {
PointerSender::PointerSender(PointerStore& pointer, PeerLink& link, Diagnostics const& diagnostics) noexcept
    : _pointer{ pointer }, _link{ link }, _diagnostics{ diagnostics } { }
auto PointerSender::Send() -> bool {
  if (_generation == _pointer.Generation()) return true;
  auto const delivery = _pointer.Send(_link.Context());
  switch (delivery) {
  case PointerDelivery::Failed: return false;
  case PointerDelivery::Unsupported:
    _diagnostics.Log(SDLRDP_LOG_WARN, "Client does not support a 384x384 pointer.");
    break;
  case PointerDelivery::Sent: break;
  default:                    utilities::Unreachable(delivery);
  }
  _generation = _pointer.Generation();
  return true;
}
}
