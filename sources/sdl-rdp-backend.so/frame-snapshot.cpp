#include "_detail/frame-snapshot.hpp"

#include "_detail/avc.hpp"
#include "_detail/contract.hpp"
#include "_detail/rect.hpp"

#include <utility>

namespace Backend {
std::size_t FrameBytes(Extent size) {
  return std::size_t(Avc::Aligned(size.width)) * Avc::Aligned(size.height) * PixelBytes;
}
FrameSnapshot::FrameSnapshot(std::shared_ptr<std::vector<BYTE> const> value, Extent size) noexcept
    : _pixels { std::move(value) }, _size{ size } { }
FrameSnapshot::operator bool() const noexcept {
  return _pixels != nullptr;
}
std::span<BYTE const> FrameSnapshot::Pixels() const noexcept {
  return _pixels ? std::span<BYTE const>(*_pixels) : std::span<BYTE const>{ };
}
std::span<BYTE const> FrameSnapshot::Row(unsigned row) const {
  Expects(_pixels != nullptr, "snapshot storage exists");
  Expects(row < _size.height, "row lies inside the snapshot");
  return Pixels().subspan(std::size_t(row) * Stride());
}
std::size_t FrameSnapshot::Stride() const {
  return std::size_t(Avc::Aligned(_size.width)) * PixelBytes;
}
unsigned FrameSnapshot::Width() const noexcept {
  return _size.width;
}
unsigned FrameSnapshot::Height() const noexcept {
  return _size.height;
}
sdlrdp_rect FrameSnapshot::Bounds() const noexcept {
  return Whole(_size);
}
FrameSnapshot FrameSnapshot::Matching(Extent size) const {
  return SameSize(Bounds(), Whole(size)) ? *this : FrameSnapshot{ nullptr, size };
}
void FrameSnapshot::Release() noexcept {
  _pixels.reset();
}
}
