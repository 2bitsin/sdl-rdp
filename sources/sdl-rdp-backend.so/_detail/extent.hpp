#pragma once
#include "sdl-rdp-backend.h"

#include <cstddef>

namespace Backend {
inline constexpr std::size_t PixelBytes = 4;
struct Extent {
  unsigned width { };
  unsigned height{ };
};
constexpr auto Whole(Extent size) noexcept -> sdlrdp_rect {
  return { 0, 0, int(size.width), int(size.height) };
}
}
