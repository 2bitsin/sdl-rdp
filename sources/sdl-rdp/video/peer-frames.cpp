#include <sdl-rdp/video/peer-frames.hpp>

#include <utility>

namespace sdl_rdp::video::detail::peer_frames {
using sdl_rdp::utilities::Expects;

PeerFrames::PeerFrames(FrameStore& store) noexcept : _store{ store } { }
auto PeerFrames::Post(FrameLock const& held, sdlrdp_rect area) -> void {
  Expects(_store.Holds(held), "posting damage holds the frame lock");
  _dirty.Add(area);
}
auto PeerFrames::Repaint(FrameLock const& held, sdlrdp_rect area) -> void {
  Expects(_store.Holds(held), "repainting holds the frame lock");
  _dirty.Clear();
  _dirty.Add(area);
}
auto PeerFrames::Refresh() -> void {
  auto const held = _store.Lock();
  Post(held, _store.Bounds(held));
}
auto PeerFrames::CountPresent(FrameLock const& held) -> void {
  Expects(_store.Holds(held), "counting a present holds the frame lock");
  ++_presents;
}
auto PeerFrames::Dirty(FrameLock const& held) const -> bool {
  Expects(_store.Holds(held), "reading damage holds the frame lock");
  return !_dirty.Rects().empty();
}
auto PeerFrames::Pending(FrameLock const& held) const -> bool {
  return Dirty(held) || _snapshot;
}
auto PeerFrames::Capture(FrameLock const& held) -> std::uint64_t {
  _snapshot = _store.Snapshot(held);
  ExpectCaptured(*this);
  _sequence = _store.Presented(held);
  _sending.Swap(_dirty);
  _dirty.Clear();
  return std::exchange(_presents, 0);
}
auto PeerFrames::Invalidate(FrameLock const& held) -> void {
  if (!Dirty(held)) return;
  _snapshot.Release();
  _dirty.Add(_store.Bounds(held));
}
auto PeerFrames::Include() -> void {
  ExpectCaptured(*this);
  _sending.Add(_snapshot.Bounds());
}
auto PeerFrames::Resend() -> void {
  _sending.Clear();
  Include();
}
auto PeerFrames::Complete(FrameLock const& held) -> void {
  Expects(_store.Holds(held), "completing a frame holds the frame lock");
  ExpectCaptured(*this);
  _snapshot.Release();
  _sending.Clear();
}
auto PeerFrames::Snapshot() const noexcept -> FrameSnapshot const& {
  return _snapshot;
}
auto PeerFrames::Sending() const noexcept -> std::vector<sdlrdp_rect> const& {
  return _sending.Rects();
}
auto PeerFrames::Sequence() const noexcept -> std::uint64_t {
  return _sequence;
}
auto ExpectCaptured(PeerFrames const& frames) -> void {
  Expects(static_cast<bool>(frames.Snapshot()), "a captured frame exists");
}
}
