#include <sdl-rdp/picture/frame-snapshot.hpp>

#include <sdl-rdp/picture/geometry.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/geometry.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace sdl_rdp::picture::detail::frame_snapshot {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::PixelBytes;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::SameSize;
using sdl_rdp::utilities::Whole;

auto FrameBytes(Extent size) -> std::size_t {
  return std::size_t{ Aligned(size.width) } * Aligned(size.height) * PixelBytes;
}
FrameSnapshot::FrameSnapshot(std::shared_ptr<std::vector<std::uint8_t> const> value, Extent size) noexcept
    : _pixels{ std::move(value) }, _size{ size } { }
FrameSnapshot::operator bool() const noexcept {
  return _pixels != nullptr;
}
auto FrameSnapshot::Pixels() const noexcept -> std::span<std::uint8_t const> {
  return _pixels ? std::span<std::uint8_t const>(*_pixels) : std::span<std::uint8_t const>{ };
}
auto FrameSnapshot::Row(std::uint32_t row) const -> std::span<std::uint8_t const> {
  Expects(_pixels != nullptr, "snapshot storage exists");
  Expects(row < _size.height, "row lies inside the snapshot");
  return Pixels().subspan(std::size_t{ row } * Stride());
}
auto FrameSnapshot::Stride() const -> std::size_t {
  return std::size_t{ Aligned(_size.width) } * PixelBytes;
}
auto FrameSnapshot::Width() const noexcept -> std::uint32_t {
  return _size.width;
}
auto FrameSnapshot::Height() const noexcept -> std::uint32_t {
  return _size.height;
}
auto FrameSnapshot::Bounds() const noexcept -> Rect {
  return Whole(_size);
}
auto FrameSnapshot::Matching(Extent size) const -> FrameSnapshot {
  return SameSize(Bounds(), Whole(size)) ? *this : FrameSnapshot{ nullptr, size };
}
auto FrameSnapshot::Release() noexcept -> void {
  _pixels.reset();
}
}
