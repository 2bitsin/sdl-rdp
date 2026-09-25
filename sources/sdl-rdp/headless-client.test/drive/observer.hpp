#pragma once
#include <sdl-rdp/drive/packet.hpp>
#include <sdl-rdp/headless-client.test/client/client.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace Headless {
struct DriveCapture {
  std::size_t                                          requests = 0;
  std::vector<sdl_rdp::drive::DrivePacket>             io;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> replies;
  bool                                                 hold     = false;
};
struct DriveObserver {
public:
           DriveObserver(DriveObserver const&)                               = delete;
           DriveObserver(DriveObserver&&)                                    = delete;
  explicit DriveObserver(Client& client);
           ~DriveObserver();
  auto     operator=(DriveObserver const&)                 -> DriveObserver& = delete;
  auto     operator=(DriveObserver&&)                      -> DriveObserver& = delete;
  auto     Send(sdl_rdp::drive::DrivePacket const& packet) -> bool;
  auto     Observed()                                      -> DriveCapture&;
  auto     Observed() const                                -> DriveCapture const&;

private:
  auto Receive(std::uint16_t id, std::span<std::byte const> data, std::uint32_t flags, std::size_t total) -> bool;
  DriveCapture                              observed;
  inline static thread_local DriveObserver* active   = nullptr;
  freerdp*                                  instance;
  pReceiveChannelData                       original;
};
}
