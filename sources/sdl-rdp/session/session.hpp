#pragma once
#include <sdl-rdp/core/frame-store.hpp>
#include <sdl-rdp/core/session-access.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/session/peer-frame.hpp>
#include <sdl-rdp/session/peer-set.hpp>

#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <type_traits>

namespace Backend {
class EventQueue;
class Session final : public SessionAccess {
public:
                     Session(FrameStore& frames, EventQueue& events);
  [[nodiscard]] auto Lock()                                               -> SessionLock   override;
  auto               ForEachPeer(std::invocable<Peer&> auto visit)        -> void;
  [[nodiscard]] auto LockPeersAndFrame()                                  -> PeerFrame;
  auto               Add(std::unique_ptr<Peer> peer)                      -> void;
  auto               Reap()                                               -> void;
  auto               ReapEvent() const noexcept                           -> WaitHandle;
  [[nodiscard]] auto Takeover(PeerLink const& self)                       -> FrameLock     override;
  auto               Depart(PeerLink const& self, Activation& activation) -> void          override;
  auto               Current(SessionLock const& held) const               -> Peer*;
  auto               Current(FrameLock const& held) const                 -> Peer*;
  auto               NextDrive() noexcept                                 -> std::uint32_t override;
  auto               AudioChanged()                                       -> void          override;
  auto               AudioGone()                                          -> void          override;
  auto WaitAudio(SessionLock& held, std::chrono::steady_clock::time_point deadline) -> void;

private:
  std::recursive_mutex        _guard;
  std::condition_variable_any _audio_changed;
  EventHandle                 _reap;
  Peer*                       _current      { };
  std::atomic<std::uint32_t>  _next_drive   { 1 };
  FrameStore&                 _frames;
  EventQueue&                 _events;
  PeerSet                     _peers;
};
auto Session::ForEachPeer(std::invocable<Peer&> auto visit) -> void {
  auto const session = Lock();
  auto const held    = _peers.Lock();
  _peers.ForEach(held, visit);
}
template <std::invocable<Peer&> Act> auto OnCurrent(Session& session, Act act) -> decltype(auto) {
  using Result = std::invoke_result_t<Act, Peer&>;
  auto const held    = session.Lock();
  auto*      current = session.Current(held);
  if constexpr (std::is_void_v<Result>) {
    if (current) act(*current);
  } else {
    return current ? act(*current) : Result{ };
  }
}
}
