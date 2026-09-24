#pragma once
#include "client.hpp"
#include <sdl-rdp/storage/drive-packet.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace Headless {
struct DriveCapture {
  std::size_t                                          requests = 0;
  std::vector<Backend::DrivePacket>                    io;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> replies;
  bool                                                 hold     = false;
};
struct DriveObserver {
public:
           DriveObserver(DriveObserver const&)                              = delete;
           DriveObserver(DriveObserver&&)                                   = delete;
  explicit DriveObserver(Client& client);
           ~DriveObserver();
  auto     operator=(DriveObserver const&)                -> DriveObserver& = delete;
  auto     operator=(DriveObserver&&)                     -> DriveObserver& = delete;
  auto     Send(Backend::DrivePacket const& packet) const -> bool;
  auto     Observed()                                     -> DriveCapture&;
  auto     Observed() const                               -> DriveCapture const&;

private:
  auto Receive(std::uint16_t id, std::span<std::byte const> data, std::uint32_t flags, std::size_t total) -> bool;
  DriveCapture                              observed;
  inline static thread_local DriveObserver* active   = nullptr;
  freerdp*                                  instance;
  pReceiveChannelData                       original;
};
}
