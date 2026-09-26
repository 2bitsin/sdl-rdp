#pragma once

namespace sdl_rdp::peer::detail::arrival {
class Arrival;
}
namespace sdl_rdp::peer::detail::channel_set {
class ChannelSet;
}
namespace sdl_rdp::peer::detail::peer {
class Peer;
}
namespace sdl_rdp::peer::detail::redirection {
class Redirection;
}
namespace sdl_rdp::peer::detail::transport_end {
class TransportEnd;
}

namespace sdl_rdp::peer {
using detail::arrival::Arrival;
using detail::channel_set::ChannelSet;
using detail::peer::Peer;
using detail::redirection::Redirection;
using detail::transport_end::TransportEnd;
}
