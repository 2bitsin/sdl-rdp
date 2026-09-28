#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/utilities/socket.hpp>

namespace sdl_rdp::auth::detail::unsignalled_socket_bio {
using sdl_rdp::freerdp_facade::Bio;
using sdl_rdp::utilities::Socket;

// The BIO holds the socket's address: the socket neither moves nor ends while the BIO lives.
auto UnsignalledSocketBio(Socket& socket) -> Bio;
}
