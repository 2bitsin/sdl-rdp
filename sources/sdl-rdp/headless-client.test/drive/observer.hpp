#pragma once
#include <sdl-rdp/drive/packet.hpp>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace sdl_rdp::headless_client_test::drive::detail::observer {
using sdl_rdp::drive::DrivePacket;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::utilities::Membership;
using sdl_rdp::utilities::Pinned;

struct DriveCapture {
  std::size_t                                          requests = 0;
  std::vector<DrivePacket>                             io;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> replies;
  bool                                                 hold     = false;
};
class DriveObserver : private Pinned {
public:
  explicit DriveObserver(Client& client);
           ~DriveObserver();
  auto     Send(DrivePacket const& packet) -> bool;
  auto     Observed()                      -> DriveCapture&;
  auto     Observed() const                -> DriveCapture const&;

private:
  auto Receive(std::uint16_t id, std::span<std::byte const> data, std::uint32_t flags, std::size_t total) -> bool;
  DriveCapture              observed;
  freerdp&                  instance;
  pReceiveChannelData       original;
  Membership<DriveObserver> membership;
};
}

namespace sdl_rdp::headless_client_test::drive {
using detail::observer::DriveObserver;
}
