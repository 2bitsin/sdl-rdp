#pragma once
#include "extent.hpp"
#include "sdl-rdp-backend.h"

#include <cstddef>
#include <memory>
#include <span>
#include <vector>
#include <winpr/wtypes.h>

namespace Backend {
std::size_t FrameBytes(Extent size);
class FrameSnapshot {
public:
                        FrameSnapshot() noexcept                                                   = default;
                        FrameSnapshot(std::shared_ptr<std::vector<BYTE> const> value, Extent size) noexcept;
  explicit              operator bool() const                                                      noexcept;
  std::span<BYTE const> Pixels() const                                                             noexcept;
  std::span<BYTE const> Row(unsigned row) const;
  std::size_t           Stride() const;
  unsigned              Width() const                                                              noexcept;
  unsigned              Height() const                                                             noexcept;
  sdlrdp_rect           Bounds() const                                                             noexcept;
  FrameSnapshot         Matching(Extent size) const;
  void                  Release()                                                                  noexcept;

private:
  std::shared_ptr<std::vector<BYTE> const> _pixels;
  Extent                                   _size;
};
}
