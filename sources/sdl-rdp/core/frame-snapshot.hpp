#pragma once
#include <sdl-rdp/utilities/extent.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <winpr/wtypes.h>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

namespace Backend {
auto FrameBytes(Extent size) -> std::size_t;
class FrameSnapshot {
public:
           FrameSnapshot() noexcept                                                   = default;
           FrameSnapshot(std::shared_ptr<std::vector<BYTE> const> value, Extent size) noexcept;
  explicit operator bool() const                                                      noexcept;
  auto     Pixels() const noexcept     -> std::span<BYTE const>;
  auto     Row(unsigned row) const     -> std::span<BYTE const>;
  auto     Stride() const              -> std::size_t;
  auto     Width() const noexcept      -> unsigned;
  auto     Height() const noexcept     -> unsigned;
  auto     Bounds() const noexcept     -> sdlrdp_rect;
  auto     Matching(Extent size) const -> FrameSnapshot;
  auto     Release() noexcept          -> void;

private:
  std::shared_ptr<std::vector<BYTE> const> _pixels;
  Extent                                   _size;
};
}
