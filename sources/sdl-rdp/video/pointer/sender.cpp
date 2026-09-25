#include <sdl-rdp/video/pointer/sender.hpp>

#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/log-level.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/video/pointer/store.hpp>

namespace sdl_rdp::video::pointer::detail::sender {
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::utilities::Unreachable;

PointerSender::PointerSender(PointerStore& pointer, PeerLink& link, Diagnostics const& diagnostics) noexcept
    : _pointer{ pointer }, _link{ link }, _diagnostics{ diagnostics } { }
auto PointerSender::Send() -> bool {
  if (_generation == _pointer.Generation()) return true;
  auto const delivery = _pointer.Send(_link.Context());
  switch (delivery) {
  case PointerDelivery::Failed: return false;
  case PointerDelivery::Unsupported:
    _diagnostics.Log(LogLevel::Warn, "Client does not support a 384x384 pointer.");
    break;
  case PointerDelivery::Sent: break;
  default:                    Unreachable(delivery);
  }
  _generation = _pointer.Generation();
  return true;
}
}
