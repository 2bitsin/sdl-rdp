#pragma once
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/picture/frame-store.hpp>

#include <cstdint>
#include <mutex>

namespace sdl_rdp::link::detail::session_access {
using sdl_rdp::picture::FrameLock;

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
  virtual auto               NextDrive() noexcept                                 -> std::uint32_t  = 0;
  virtual auto               AudioChanged()                                       -> void           = 0;
  virtual auto               AudioGone()                                          -> void           = 0;
};
}

namespace sdl_rdp::link {
using detail::session_access::SessionAccess;
using detail::session_access::SessionLock;
}
