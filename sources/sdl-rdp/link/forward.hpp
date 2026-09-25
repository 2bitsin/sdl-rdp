#pragma once

namespace sdl_rdp::link::detail::activation {
class Activation;
}
namespace sdl_rdp::link::detail::event_queue {
class EventQueue;
}
namespace sdl_rdp::link::detail::peer_link {
class PeerLink;
}
namespace sdl_rdp::link::detail::session_access {
class SessionAccess;
}

namespace sdl_rdp::link {
using detail::activation::Activation;
using detail::event_queue::EventQueue;
using detail::peer_link::PeerLink;
using detail::session_access::SessionAccess;
}
