#pragma once

namespace sdl_rdp::peer::detail::activator {
class Activator;
}
namespace sdl_rdp::peer::detail::arrival {
class Arrival;
}
namespace sdl_rdp::peer::detail::capability_check {
class CapabilityCheck;
}
namespace sdl_rdp::peer::detail::channel_set {
class ChannelSet;
}
namespace sdl_rdp::peer::detail::departure {
class Departure;
}
namespace sdl_rdp::peer::detail::peer {
class Peer;
}
namespace sdl_rdp::peer::detail::pump {
class PeerPump;
}
namespace sdl_rdp::peer::detail::redirection {
class Redirection;
}
namespace sdl_rdp::peer::detail::transport_end {
class TransportEnd;
}
namespace sdl_rdp::peer::detail::wait {
class PeerWait;
}

namespace sdl_rdp::peer {
using detail::activator::Activator;
using detail::arrival::Arrival;
using detail::capability_check::CapabilityCheck;
using detail::channel_set::ChannelSet;
using detail::departure::Departure;
using detail::peer::Peer;
using detail::pump::PeerPump;
using detail::redirection::Redirection;
using detail::transport_end::TransportEnd;
using detail::wait::PeerWait;
}
