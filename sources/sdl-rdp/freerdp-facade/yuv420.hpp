#pragma once
#include <sdl-rdp/utilities/geometry.hpp>

#include <cstdint>
#include <span>

namespace sdl_rdp::freerdp_facade::detail::yuv420 {
using sdl_rdp::utilities::Extent;

// Y at full resolution, U (Cb) and V (Cr) at half width and half height.
struct Yuv420Planes {
  std::span<std::uint8_t> luma;
  std::span<std::uint8_t> blue_difference;
  std::span<std::uint8_t> red_difference;
  std::uint32_t           luma_pitch     { };
  std::uint32_t           chroma_pitch   { };
};
// I420 in one buffer: Y, then U and V at half the pitch and half the height.
auto PackedI420(std::span<std::uint8_t> frame, std::uint32_t pitch, std::uint32_t height) -> Yuv420Planes;
// BT.709 through FreeRDP's primitives, the one place the facade calls them.
auto RgbToYuv420(std::span<std::uint8_t const> bgrx, std::uint32_t stride, Extent size, Yuv420Planes const& planes)
    -> bool;
}

namespace sdl_rdp::freerdp_facade {
using detail::yuv420::PackedI420;
using detail::yuv420::RgbToYuv420;
using detail::yuv420::Yuv420Planes;
}
