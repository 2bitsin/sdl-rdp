#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <cstddef>
#include <cstdint>

namespace Backend {
inline constexpr std::size_t PixelBytes = 4;
struct Extent {
  std::uint32_t width { };
  std::uint32_t height{ };
};
constexpr auto Whole(Extent size) noexcept -> sdlrdp_rect {
  return { 0, 0, Narrowed<int>(size.width), Narrowed<int>(size.height) };
}
}
