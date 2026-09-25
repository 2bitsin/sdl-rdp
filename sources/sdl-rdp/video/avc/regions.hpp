#pragma once
#include <sdl-rdp/utilities/rect.hpp>

#include <freerdp/channels/rdpgfx.h>
#include <cstddef>
#include <vector>

namespace sdl_rdp::video::avc::detail::regions {
using sdl_rdp::utilities::Rect;
class Regions {
public:
  auto Add(Rect area) -> void;
  auto Bytes() const  -> std::size_t;
  auto Areas()        -> std::vector<RECTANGLE_16>&;
  auto Quality()      -> std::vector<RDPGFX_H264_QUANT_QUALITY>&;
  auto Bounds() const -> Rect;
  auto Clear()        -> void;

private:
  std::vector<RECTANGLE_16>              areas;
  std::vector<RDPGFX_H264_QUANT_QUALITY> quality;
  Rect                                   bounds { };
};
}

namespace sdl_rdp::video::avc {
using detail::regions::Regions;
}
