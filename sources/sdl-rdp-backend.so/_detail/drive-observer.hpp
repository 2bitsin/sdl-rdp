#pragma once
#include "client.hpp"
#include "drive-packet.hpp"

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
                      DriveObserver(DriveObserver const&) = delete;
                      DriveObserver(DriveObserver&&)      = delete;
  explicit            DriveObserver(Client& client);
                      ~DriveObserver();
  DriveObserver&      operator = (DriveObserver const&)   = delete;
  DriveObserver&      operator = (DriveObserver&&)        = delete;
  bool                Send(Backend::DrivePacket const& packet) const;
  DriveCapture&       Observed();
  DriveCapture const& Observed() const;

private:
  static BOOL Receive(freerdp* instance, UINT16 id, BYTE const* data, size_t size, UINT32 flags, size_t total);
  DriveCapture                              observed;
  inline static thread_local DriveObserver* active   = nullptr;
  freerdp*                                  instance;
  pReceiveChannelData                       original;
};
}
