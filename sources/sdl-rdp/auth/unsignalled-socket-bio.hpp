#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>

namespace sdl_rdp::auth::detail::unsignalled_socket_bio {
using sdl_rdp::freerdp_facade::Bio;

auto UnsignalledSocketBio(int socket) -> Bio;
}
