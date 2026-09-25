#pragma once
#include <sdl-rdp/utilities/descriptor.hpp>

#include <utility>

namespace sdl_rdp::utilities::detail::socket_pair {
class SocketPair {
public:
                     SocketPair();
  explicit           SocketPair(std::pair<Descriptor, Descriptor> connected);
  [[nodiscard]] auto TakeServer()            -> Descriptor;
  [[nodiscard]] auto Client() const noexcept -> int;

private:
  Descriptor server;
  Descriptor client;
};
}

namespace sdl_rdp::utilities {
using detail::socket_pair::SocketPair;
}
