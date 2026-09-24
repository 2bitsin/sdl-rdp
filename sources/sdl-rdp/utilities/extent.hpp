#pragma once
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <cstddef>
#include <cstdint>

namespace Backend {
inline constexpr std::size_t PixelBytes = 4;
struct Extent {
  std::uint32_t width { };
  std::uint32_t height{ };
};
constexpr auto Whole(Extent size) noexcept -> sdlrdp_rect {
  return { 0, 0, int(size.width), int(size.height) };
}
}
