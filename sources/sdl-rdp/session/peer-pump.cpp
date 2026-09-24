#include <sdl-rdp/session/peer-pump.hpp>

#include <sdl-rdp/core/peer-link.hpp>
#include <sdl-rdp/core/session-access.hpp>
#include <sdl-rdp/core/trace-queue.hpp>
#include <sdl-rdp/session/channel-set.hpp>
#include <sdl-rdp/session/redirection.hpp>
#include <sdl-rdp/session/transport-end.hpp>
#include <sdl-rdp/video/frame-sender.hpp>

namespace Backend {
PeerPump::PeerPump(PeerLink& link, SessionAccess& session, ChannelSet& channels, Redirection& redirection,
                   FrameSender& sender, TransportEnd& end, TraceQueue& traces) noexcept
    : _link{ link }, _session{ session }, _channels{ channels }, _redirection{ redirection }, _sender{ sender },
      _end{ end }, _traces{ traces } { }
auto PeerPump::Service(std::stop_token const& quit, std::span<HANDLE const> ready) -> bool {
  auto const healthy = Exchange(quit, ready) && Deliver(quit);
  _traces.Flush();
  return healthy;
}
auto PeerPump::Ended() -> bool {
  _end.Report();
  return false;
}
auto PeerPump::Exchange(std::stop_token const& quit, std::span<HANDLE const> ready) -> bool {
  auto const session = _session.Lock();
  if (quit.stop_requested()) return false;
  auto& client = _link.Client();
  if (!client.CheckFileDescriptor(&client) || !_channels.Pump(ready)) return Ended();
  _redirection.Sound(ready);
  return _sender.Drain() || Ended();
}
auto PeerPump::Deliver(std::stop_token const& quit) -> bool {
  auto const delivery = _sender.Encode(quit);
  switch (delivery) {
  case Delivery::Healthy: return true;
  case Delivery::Stopped: return false;
  case Delivery::Failed: {
    auto const session = _session.Lock();
    return Ended();
  }
  default: utilities::Unreachable(delivery);
  }
}
}
