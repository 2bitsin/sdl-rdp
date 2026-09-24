#include "_detail/avc-regions.hpp"
#include "_detail/contract.hpp"

#include <algorithm>

namespace Backend::Avc {
using utilities::Expects;
void Regions::Add(sdlrdp_rect area) {
  Expects(area.x >= 0, "region left edge is nonnegative");
  Expects(area.y >= 0, "region top edge is nonnegative");
  Expects(area.w > 0, "region width is positive");
  Expects(area.h > 0, "region height is positive");
  Expects(area.x + area.w <= 32766, "region right edge fits wire");
  Expects(area.y + area.h <= 32766, "region bottom edge fits wire");
  if (rects.empty())
    bounds = area;
  else {
    auto right  = std::max(bounds.x + bounds.w, area.x + area.w);
    auto bottom = std::max(bounds.y + bounds.h, area.y + area.h);
    bounds.x = std::min(bounds.x, area.x);
    bounds.y = std::min(bounds.y, area.y);
    bounds.w = right - bounds.x;
    bounds.h = bottom - bounds.y;
  }
  rects.push_back({ UINT16(area.x), UINT16(area.y), UINT16(area.x + area.w), UINT16(area.y + area.h) });
  quality.push_back({ 0x9a, 100, 26, 0, 1 });
}
std::size_t Regions::Bytes() const {
  Expects(rects.size() == quality.size(), "every region has quantization metadata");
  return 4 + (10 * rects.size());
}
std::vector<RECTANGLE_16>& Regions::Rects() { return rects; }
std::vector<RDPGFX_H264_QUANT_QUALITY>& Regions::Quality() { return quality; }
sdlrdp_rect Regions::Bounds() const { return bounds; }
void Regions::Clear() {
  rects.clear();
  quality.clear();
}
}
