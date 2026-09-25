#pragma once
#include <cstddef>
#include <cstdint>

namespace sdl_rdp::utilities::detail::extent {
inline constexpr std::size_t PixelBytes = 4;
struct Extent {
  std::uint32_t width { };
  std::uint32_t height{ };
};
}

namespace sdl_rdp::utilities {
using detail::extent::Extent;
using detail::extent::PixelBytes;
}
