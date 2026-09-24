#pragma once
#include <sdl-rdp/utilities/descriptor.hpp>

#include <utility>

namespace Backend {
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
