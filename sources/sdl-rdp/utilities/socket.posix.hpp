#pragma once
#include <sdl-rdp/utilities/socket.hpp>

namespace sdl_rdp::utilities::detail::socket {
// Close-on-exec from the call where the kernel takes the flag (Linux), right after it where it does not (macOS).
auto OpenedSocket(int domain, int type) -> Socket;
auto OpenedPair(int domain, int type)   -> SocketPair;
}
