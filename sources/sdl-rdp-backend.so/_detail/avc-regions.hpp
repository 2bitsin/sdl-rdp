#pragma once
#include "sdl-rdp-backend.h"

#include <cstddef>
#include <freerdp/channels/rdpgfx.h>
#include <vector>

namespace Backend::Avc {
class Regions {
public:
  void                                    Add(sdlrdp_rect area);
  std::size_t                             Bytes() const;
  std::vector<RECTANGLE_16>&              Rects();
  std::vector<RDPGFX_H264_QUANT_QUALITY>& Quality();
  sdlrdp_rect                             Bounds() const;
  void                                    Clear();

private:
  std::vector<RECTANGLE_16>              rects;
  std::vector<RDPGFX_H264_QUANT_QUALITY> quality;
  sdlrdp_rect                            bounds { };
};
} // namespace Backend::Avc
