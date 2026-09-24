#pragma once
#include "frame-store.hpp"

#include <mutex>

namespace Backend {
class Activation;
class PeerLink;
using SessionLock = std::unique_lock<std::recursive_mutex>;
class SessionAccess {
public:
                                    SessionAccess()                                      = default;
                                    SessionAccess(SessionAccess const&)                  = delete;
                                    SessionAccess(SessionAccess&&)                       = delete;
  virtual                           ~SessionAccess()                                     = default;
  SessionAccess&                    operator = (SessionAccess const&)                    = delete;
  SessionAccess&                    operator = (SessionAccess&&)                         = delete;
  [[nodiscard]] virtual SessionLock Lock()                                               = 0;
  [[nodiscard]] virtual FrameLock   Takeover(PeerLink const& self)                       = 0;
  virtual void                      Depart(PeerLink const& self, Activation& activation) = 0;
  virtual unsigned                  NextDrive() noexcept                                 = 0;
  virtual void                      AudioChanged()                                       = 0;
  virtual void                      AudioGone()                                          = 0;
};
}
