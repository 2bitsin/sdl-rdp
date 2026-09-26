#pragma once
#include <string_view>

namespace sdl_rdp::utilities::detail::posix {
class Descriptor {
public:
  explicit           Descriptor(int owned)                                 noexcept;
                     Descriptor(Descriptor&& other)                        noexcept;
                     Descriptor(Descriptor const&)                         = delete;
                     ~Descriptor();
  auto               operator=(Descriptor&& other) noexcept -> Descriptor&;
  auto               operator=(Descriptor const&)           -> Descriptor& = delete;
  [[nodiscard]] auto Owns() const noexcept                  -> bool;
  [[nodiscard]] auto Get() const noexcept                   -> int;
  [[nodiscard]] auto Release() noexcept                     -> int;

private:
  static constexpr int Closed     = -1;
  int                  descriptor;
};
struct SocketPair {
  Descriptor server;
  Descriptor client;
};
auto SystemCall(int result, std::string_view operation) -> int;
auto ConnectedSockets()                                 -> SocketPair;
}

namespace sdl_rdp::utilities {
using detail::posix::ConnectedSockets;
using detail::posix::Descriptor;
using detail::posix::SocketPair;
using detail::posix::SystemCall;
}
