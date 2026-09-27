#include <sdl-rdp/freerdp-facade/yuv420.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <freerdp/codec/color.h>
#include <freerdp/primitives.h>
#include <array>
#include <cstddef>

namespace sdl_rdp::freerdp_facade::detail::yuv420 {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Stride;

namespace {
auto Covers(std::span<std::uint8_t> plane, std::uint32_t pitch, std::uint32_t rows) -> bool {
  return plane.size() >= std::size_t{ pitch } * rows;
}
}
auto PackedI420(std::span<std::uint8_t> frame, std::uint32_t pitch, std::uint32_t height) -> Yuv420Planes {
  Expects(pitch % 2 == 0, "I420 pitch is even");
  Expects(height % 2 == 0, "I420 height is even");
  auto const luma   = std::size_t{ pitch } * height;
  auto const chroma = luma / 4;
  Expects(frame.size() >= luma + (2 * chroma), "I420 frame holds three planes");
  return { .luma            = frame.first(luma),
           .blue_difference = frame.subspan(luma, chroma),
           .red_difference  = frame.subspan(luma + chroma, chroma),
           .luma_pitch      = pitch,
           .chroma_pitch    = pitch / 2 };
}
auto RgbToYuv420(std::span<std::uint8_t const> bgrx, std::uint32_t stride, Extent size, Yuv420Planes const& planes)
    -> bool {
  Expects(size.width % 2 == 0, "4:2:0 width is even");
  Expects(size.height % 2 == 0, "4:2:0 height is even");
  auto const row = Stride(size.width);
  Expects(stride >= row, "source stride covers the width");
  Expects(bgrx.size() >= (std::size_t{ stride } * (size.height - 1)) + row, "source covers the height");
  Expects(planes.luma_pitch >= size.width, "luma pitch covers the width");
  Expects(planes.chroma_pitch >= size.width / 2, "chroma pitch covers half the width");
  auto const luma = Covers(planes.luma, planes.luma_pitch, size.height);
  auto const blue = Covers(planes.blue_difference, planes.chroma_pitch, size.height / 2);
  auto const red  = Covers(planes.red_difference, planes.chroma_pitch, size.height / 2);
  Expects(luma, "luma plane covers the height");
  Expects(blue, "U plane covers half the height");
  Expects(red, "V plane covers half the height");
  std::array        destinations{ planes.luma.data(), planes.blue_difference.data(), planes.red_difference.data() };
  std::array const  pitches     { planes.luma_pitch, planes.chroma_pitch, planes.chroma_pitch                     };
  prim_size_t const region      { size.width, size.height                                                         };
  return primitives_get()->RGBToYUV420_8u_P3AC4R(bgrx.data(), PIXEL_FORMAT_BGRX32, stride, destinations.data(),
                                                 pitches.data(), &region)
         == PRIMITIVES_SUCCESS;
}
}
