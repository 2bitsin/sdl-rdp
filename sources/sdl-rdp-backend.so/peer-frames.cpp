#include "_detail/peer-frames.hpp"

#include <utility>

namespace Backend {
PeerFrames::PeerFrames(FrameStore& store) noexcept : _store{ store } { }
void PeerFrames::Post(FrameLock const& held, sdlrdp_rect area) {
  Expects(_store.Holds(held), "posting damage holds the frame lock");
  _dirty.Add(area);
}
void PeerFrames::Repaint(FrameLock const& held, sdlrdp_rect area) {
  Expects(_store.Holds(held), "repainting holds the frame lock");
  _dirty.clear();
  _dirty.Add(area);
}
void PeerFrames::Refresh() {
  auto const held = _store.Lock();
  Post(held, _store.Bounds(held));
}
void PeerFrames::CountPresent(FrameLock const& held) {
  Expects(_store.Holds(held), "counting a present holds the frame lock");
  ++_presents;
}
bool PeerFrames::Dirty(FrameLock const& held) const {
  Expects(_store.Holds(held), "reading damage holds the frame lock");
  return !_dirty.empty();
}
bool PeerFrames::Pending(FrameLock const& held) const {
  return Dirty(held) || _snapshot;
}
uint64_t PeerFrames::Capture(FrameLock const& held) {
  _snapshot = _store.Snapshot(held);
  ExpectCaptured(*this);
  _sequence = _store.Presented(held);
  _sending.Swap(_dirty);
  _dirty.clear();
  return std::exchange(_presents, 0);
}
void PeerFrames::Invalidate(FrameLock const& held) {
  if (!Dirty(held)) return;
  _snapshot.Release();
  _dirty.Add(_store.Bounds(held));
}
void PeerFrames::Include() {
  ExpectCaptured(*this);
  _sending.Add(_snapshot.Bounds());
}
void PeerFrames::Resend() {
  _sending.clear();
  Include();
}
void PeerFrames::Complete(FrameLock const& held) {
  Expects(_store.Holds(held), "completing a frame holds the frame lock");
  ExpectCaptured(*this);
  _snapshot.Release();
  _sending.clear();
}
FrameSnapshot const& PeerFrames::Snapshot() const noexcept {
  return _snapshot;
}
std::vector<sdlrdp_rect> const& PeerFrames::Sending() const noexcept {
  return _sending.Rects();
}
uint64_t PeerFrames::Sequence() const noexcept {
  return _sequence;
}
void ExpectCaptured(PeerFrames const& frames) {
  Expects(bool(frames.Snapshot()), "a captured frame exists");
}
}
