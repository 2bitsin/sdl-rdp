#pragma once
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <freerdp/channels/rdpgfx.h>
#include <cstddef>
#include <vector>

namespace Backend::Avc {
class Regions {
public:
  auto Add(sdlrdp_rect area) -> void;
  auto Bytes() const         -> std::size_t;
  auto Rects()               -> std::vector<RECTANGLE_16>&;
  auto Quality()             -> std::vector<RDPGFX_H264_QUANT_QUALITY>&;
  auto Bounds() const        -> sdlrdp_rect;
  auto Clear()               -> void;

private:
  std::vector<RECTANGLE_16>              rects;
  std::vector<RDPGFX_H264_QUANT_QUALITY> quality;
  sdlrdp_rect                            bounds { };
};
} // namespace Backend::Avc
