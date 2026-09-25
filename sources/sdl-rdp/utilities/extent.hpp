#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <cstddef>
#include <cstdint>

namespace sdl_rdp::utilities::detail::extent {
inline constexpr std::size_t PixelBytes = 4;
struct Extent {
  std::uint32_t width { };
  std::uint32_t height{ };
};
constexpr auto Whole(Extent size) noexcept -> sdlrdp_rect {
  return { 0, 0, Narrowed<int>(size.width), Narrowed<int>(size.height) };
}
}

namespace sdl_rdp::utilities {
using detail::extent::Extent;
using detail::extent::PixelBytes;
using detail::extent::Whole;
}
