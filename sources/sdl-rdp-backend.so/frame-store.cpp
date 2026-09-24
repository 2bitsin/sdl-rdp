#include "_detail/frame-store.hpp"

#include "_detail/rect.hpp"

#include <utility>

namespace Backend {
namespace {
bool Consistent(FrameSnapshot const& shadow, PictureGeometry const& geometry) {
  return !shadow || SameSize(shadow.Bounds(), geometry.Bounds());
}
FrameSnapshot Blank(sdlrdp_rect bounds) {
  Extent const size{ .width = unsigned(bounds.w), .height = unsigned(bounds.h) };
  return { std::make_shared<std::vector<BYTE> const>(FrameBytes(size)), size };
}
}
FrameStore::FrameStore(Extent size, sdlrdp_aspect aspect) : _geometry{ size, aspect } { }
FrameLock FrameStore::Lock() {
  return FrameLock{ _guard };
}
bool FrameStore::Holds(FrameLock const& held) const noexcept {
  return held.owns_lock() && held.mutex() == &_guard;
}
void FrameStore::Notify() {
  _changed.notify_all();
}
sdlrdp_rect FrameStore::Picture(FrameLock const& held) const {
  Expects(Holds(held), "reading the picture holds the frame lock");
  return _geometry.Desktop();
}
sdlrdp_rect FrameStore::Bounds(FrameLock const& held) const {
  Expects(Holds(held), "reading the bounds holds the frame lock");
  return _geometry.Bounds();
}
FrameSnapshot const& FrameStore::Snapshot(FrameLock const& held) const {
  Expects(Holds(held), "reading the shadow holds the frame lock");
  return _shadow;
}
FrameSnapshot FrameStore::Previous(FrameLock const& held, Extent size) const {
  Expects(Holds(held), "reading the shadow holds the frame lock");
  return _shadow.Matching(size);
}
uint64_t FrameStore::Presented(FrameLock const& held) const {
  Expects(Holds(held), "reading the present count holds the frame lock");
  return _presented;
}
bool FrameStore::Publish(FrameLock const& held, std::shared_ptr<std::vector<BYTE> const> next, Extent size) {
  Expects(Holds(held), "publishing holds the frame lock");
  auto const resized = !SameSize(_shadow.Bounds(), Whole(size));
  std::ignore = _geometry.Resize(size);
  _shadow     = { std::move(next), size };
  ++_presented;
  Ensures(Consistent(_shadow, _geometry), "the shadow has the picture's size");
  return resized;
}
bool FrameStore::Ensure(FrameLock const& held) {
  Expects(Holds(held), "creating a picture holds the frame lock");
  if (_shadow) return false;
  _shadow = Blank(_geometry.Bounds());
  Ensures(Consistent(_shadow, _geometry), "the shadow has the picture's size");
  return true;
}
bool FrameStore::Resize(FrameLock const& held, Extent size) {
  Expects(Holds(held), "resizing holds the frame lock");
  if (!_geometry.Resize(size)) return false;
  _shadow = Blank(_geometry.Bounds());
  ++_presented;
  Ensures(Consistent(_shadow, _geometry), "the shadow has the picture's size");
  return true;
}
void FrameStore::SetAspect(FrameLock const& held, sdlrdp_aspect value) {
  Expects(Holds(held), "changing the aspect holds the frame lock");
  _geometry.SetAspect(value);
}
}
