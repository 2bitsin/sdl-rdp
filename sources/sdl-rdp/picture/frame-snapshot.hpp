#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/utilities/extent.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Backend {
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
  auto     Bounds() const noexcept      -> sdlrdp_rect;
  auto     Matching(Extent size) const  -> FrameSnapshot;
  auto     Release() noexcept           -> void;

private:
  std::shared_ptr<std::vector<std::uint8_t> const> _pixels;
  Extent                                           _size;
};
}
