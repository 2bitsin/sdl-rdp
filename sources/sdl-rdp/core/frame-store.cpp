#include <sdl-rdp/core/frame-store.hpp>

#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/rect.hpp>

#include <cstdint>
#include <utility>

namespace Backend {
namespace {
auto Consistent(FrameSnapshot const& shadow, PictureGeometry const& geometry) -> bool {
  return !shadow || SameSize(shadow.Bounds(), geometry.Bounds());
}
auto Blank(sdlrdp_rect bounds) -> FrameSnapshot {
  Extent const size{ .width = Narrowed<std::uint32_t>(bounds.w), .height = Narrowed<std::uint32_t>(bounds.h) };
  return { std::make_shared<std::vector<std::uint8_t> const>(FrameBytes(size)), size };
}
}
FrameStore::FrameStore(Extent size, sdlrdp_aspect aspect) : _geometry{ size, aspect } { }
auto FrameStore::Lock() -> FrameLock {
  return FrameLock{ _guard };
}
auto FrameStore::Holds(FrameLock const& held) const noexcept -> bool {
  return held.owns_lock() && held.mutex() == &_guard;
}
auto FrameStore::Notify() -> void {
  _changed.notify_all();
}
auto FrameStore::Picture(FrameLock const& held) const -> sdlrdp_rect {
  Expects(Holds(held), "reading the picture holds the frame lock");
  return _geometry.Desktop();
}
auto FrameStore::Bounds(FrameLock const& held) const -> sdlrdp_rect {
  Expects(Holds(held), "reading the bounds holds the frame lock");
  return _geometry.Bounds();
}
auto FrameStore::Snapshot(FrameLock const& held) const -> FrameSnapshot const& {
  Expects(Holds(held), "reading the shadow holds the frame lock");
  return _shadow;
}
auto FrameStore::Previous(FrameLock const& held, Extent size) const -> FrameSnapshot {
  Expects(Holds(held), "reading the shadow holds the frame lock");
  return _shadow.Matching(size);
}
auto FrameStore::Presented(FrameLock const& held) const -> std::uint64_t {
  Expects(Holds(held), "reading the present count holds the frame lock");
  return _presented;
}
auto FrameStore::Publish(FrameLock const& held, std::shared_ptr<std::vector<std::uint8_t> const> next, Extent size)
    -> bool {
  Expects(Holds(held), "publishing holds the frame lock");
  auto const resized = !SameSize(_shadow.Bounds(), Whole(size));
  std::ignore = _geometry.Resize(size);
  _shadow     = { std::move(next), size };
  ++_presented;
  Ensures(Consistent(_shadow, _geometry), "the shadow has the picture's size");
  return resized;
}
auto FrameStore::Ensure(FrameLock const& held) -> bool {
  Expects(Holds(held), "creating a picture holds the frame lock");
  if (_shadow) return false;
  _shadow = Blank(_geometry.Bounds());
  Ensures(Consistent(_shadow, _geometry), "the shadow has the picture's size");
  return true;
}
auto FrameStore::Resize(FrameLock const& held, Extent size) -> bool {
  Expects(Holds(held), "resizing holds the frame lock");
  if (!_geometry.Resize(size)) return false;
  _shadow = Blank(_geometry.Bounds());
  ++_presented;
  Ensures(Consistent(_shadow, _geometry), "the shadow has the picture's size");
  return true;
}
auto FrameStore::SetAspect(FrameLock const& held, sdlrdp_aspect value) -> void {
  Expects(Holds(held), "changing the aspect holds the frame lock");
  _geometry.SetAspect(value);
}
}
