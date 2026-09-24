#pragma once
#include "frame-store.hpp"
#include "peer-set.hpp"
#include "session-access.hpp"
#include "rdp-handles.hpp"

#include <atomic>
#include <chrono>
#include <concepts>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <type_traits>

namespace Backend {
class EventQueue;
class Session final : public SessionAccess {
public:
                            Session(FrameStore& frames, EventQueue& events);
  [[nodiscard]] SessionLock Lock() override;
  [[nodiscard]] PeersLock   LockPeers();
  void                      ForEach(PeersLock const& held, std::invocable<Peer&> auto visit) {
    _peers.ForEach(held, visit);
  }
  void                    Add(std::unique_ptr<Peer> peer);
  void                    Reap();
  HANDLE                  ReapEvent() const                                    noexcept;
  [[nodiscard]] FrameLock Takeover(PeerLink const& self)                       override;
  void                    Depart(PeerLink const& self, Activation& activation) override;
  Peer*                   Current(SessionLock const& held) const;
  Peer*                   Current(FrameLock const& held) const;
  unsigned                NextDrive() noexcept                                 override;
  void                    AudioChanged()                                       override;
  void                    AudioGone()                                          override;
  void                    WaitAudio(SessionLock& held, std::chrono::steady_clock::time_point deadline);

private:
  std::recursive_mutex        _guard;
  std::condition_variable_any _audio_changed;
  EventHandle                 _reap;
  Peer*                       _current      { };
  std::atomic_uint            _next_drive   { 1 };
  FrameStore&                 _frames;
  EventQueue&                 _events;
  PeerSet                     _peers;
};
template <std::invocable<Peer&> Act> auto OnCurrent(Session& session, Act act) {
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
