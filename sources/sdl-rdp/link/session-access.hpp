#pragma once
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/picture/frame-store.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstdint>
#include <mutex>

namespace sdl_rdp::link::detail::session_access {
using sdl_rdp::picture::FrameLock;
using sdl_rdp::utilities::Pinned;

using SessionLock = std::unique_lock<std::recursive_mutex>;
class SessionAccess : private Pinned {
public:
  virtual                    ~SessionAccess()                                                      = default;
  [[nodiscard]] virtual auto Lock()                                               -> SessionLock   = 0;
  [[nodiscard]] virtual auto Takeover(PeerLink const& self)                       -> FrameLock     = 0;
  virtual auto               Depart(PeerLink const& self, Activation& activation) -> void          = 0;
  virtual auto               NextDrive() noexcept                                 -> std::uint32_t = 0;
  virtual auto               AudioChanged()                                       -> void          = 0;
  virtual auto               AudioGone()                                          -> void          = 0;
};
}

namespace sdl_rdp::link {
using detail::session_access::SessionAccess;
using detail::session_access::SessionLock;
}
