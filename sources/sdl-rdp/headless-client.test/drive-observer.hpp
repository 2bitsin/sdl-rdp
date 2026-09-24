#pragma once
#include "client.hpp"
#include <sdl-rdp/storage/drive-packet.hpp>

#include <utility>
#include <vector>

namespace Headless {
struct DriveCapture {
  unsigned                                   requests = 0;
  std::vector<Backend::DrivePacket>          io;
  std::vector<std::pair<unsigned, unsigned>> replies;
  bool                                       hold     = false;
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
  static auto Receive(freerdp* instance, UINT16 id, BYTE const* data, size_t size, UINT32 flags, size_t total) -> BOOL;
  DriveCapture                              observed;
  inline static thread_local DriveObserver* active   = nullptr;
  freerdp*                                  instance;
  pReceiveChannelData                       original;
};
}
