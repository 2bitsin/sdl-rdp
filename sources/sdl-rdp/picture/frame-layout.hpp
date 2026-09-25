#pragma once
#include <sdl-rdp/utilities/extent.hpp>

#include <cstddef>
#include <cstdint>

namespace sdl_rdp::picture::detail::frame_layout {
// A caller's frame: its size within RDP's limits and a pitch that covers each row.
class FrameLayout {
public:
       FrameLayout(std::uint32_t width, std::uint32_t height, int pitch);
  auto Size() const noexcept  -> Backend::Extent;
  auto Pitch() const noexcept -> std::uint32_t;
  auto Bytes() const noexcept -> std::size_t;

private:
  Backend::Extent _size;
  std::uint32_t   _pitch;
};
}
namespace sdl_rdp::picture {
using detail::frame_layout::FrameLayout;
}
