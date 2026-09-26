#pragma once
#include <sdl-rdp/utilities/geometry.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sdl_rdp::picture::detail::frame_snapshot {
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Rect;

auto FrameBytes(Extent size) -> std::size_t;
class FrameSnapshot {
public:
           FrameSnapshot() noexcept                                                           = default;
           FrameSnapshot(std::shared_ptr<std::vector<std::uint8_t> const> value, Extent size) noexcept;
  explicit operator bool() const                                                              noexcept;
  auto     Pixels() const noexcept      -> std::span<std::uint8_t const>;
  auto     Row(std::uint32_t row) const -> std::span<std::uint8_t const>;
  auto     Stride() const               -> std::size_t;
  auto     Width() const noexcept       -> std::uint32_t;
  auto     Height() const noexcept      -> std::uint32_t;
  auto     Bounds() const noexcept      -> Rect;
  auto     Matching(Extent size) const  -> FrameSnapshot;
  auto     Release() noexcept           -> void;

private:
  std::shared_ptr<std::vector<std::uint8_t> const> _pixels;
  Extent                                           _size;
};
}

namespace sdl_rdp::picture {
using detail::frame_snapshot::FrameBytes;
using detail::frame_snapshot::FrameSnapshot;
}
