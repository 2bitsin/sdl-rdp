#pragma once
#include "frame-store.hpp"

#include <mutex>

namespace Backend {
class Activation;
class PeerLink;
using SessionLock = std::unique_lock<std::recursive_mutex>;
class SessionAccess {
public:
                             SessionAccess()                                                        = default;
                             SessionAccess(SessionAccess const&)                                    = delete;
                             SessionAccess(SessionAccess&&)                                         = delete;
  virtual                    ~SessionAccess()                                                       = default;
  auto                       operator=(SessionAccess const&)                      -> SessionAccess& = delete;
  auto                       operator=(SessionAccess&&)                           -> SessionAccess& = delete;
  [[nodiscard]] virtual auto Lock()                                               -> SessionLock    = 0;
  [[nodiscard]] virtual auto Takeover(PeerLink const& self)                       -> FrameLock      = 0;
  virtual auto               Depart(PeerLink const& self, Activation& activation) -> void           = 0;
  virtual auto               NextDrive() noexcept                                 -> unsigned       = 0;
  virtual auto               AudioChanged()                                       -> void           = 0;
  virtual auto               AudioGone()                                          -> void           = 0;
};
}
