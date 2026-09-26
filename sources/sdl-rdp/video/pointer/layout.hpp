#pragma once
#include <sdl-rdp/utilities/geometry.hpp>

#include <cstddef>
#include <cstdint>

namespace sdl_rdp::video::pointer::detail::layout {
using sdl_rdp::utilities::Extent;

inline constexpr std::uint32_t LargePointerLimit = 384;
// A cursor's size and hotspot: empty hides it; otherwise within a large pointer, the hotspot inside.
class PointerLayout {
public:
       PointerLayout(Extent size, std::uint32_t x, std::uint32_t y);
  auto Size() const noexcept  -> Extent;
  auto X() const noexcept     -> std::uint32_t;
  auto Y() const noexcept     -> std::uint32_t;
  auto Bytes() const noexcept -> std::size_t;

private:
  Extent        _size;
  std::uint32_t _x;
  std::uint32_t _y;
};
}

namespace sdl_rdp::video::pointer {
using detail::layout::LargePointerLimit;
using detail::layout::PointerLayout;
}
