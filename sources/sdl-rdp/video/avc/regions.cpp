#include <sdl-rdp/video/avc/regions.hpp>

#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace sdl_rdp::video::avc::detail::regions {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::Union;
auto Regions::Add(Rect area) -> void {
  Expects(area.x >= 0, "region left edge is nonnegative");
  Expects(area.y >= 0, "region top edge is nonnegative");
  Expects(area.w > 0, "region width is positive");
  Expects(area.h > 0, "region height is positive");
  Expects(area.x + area.w <= 32766, "region right edge fits wire");
  Expects(area.y + area.h <= 32766, "region bottom edge fits wire");
  bounds = areas.empty() ? area : Union(bounds, area);
  areas.push_back({ Narrowed<std::uint16_t>(area.x), Narrowed<std::uint16_t>(area.y),
                    Narrowed<std::uint16_t>(area.x + area.w), Narrowed<std::uint16_t>(area.y + area.h) });
  quality.push_back({ 0x9a, 100, 26, 0, 1 });
}
auto Regions::Bytes() const -> std::size_t {
  Expects(areas.size() == quality.size(), "every region has quantization metadata");
  return 4 + (10 * areas.size());
}
auto Regions::Areas() -> std::vector<RECTANGLE_16>& {
  return areas;
}
auto Regions::Quality() -> std::vector<RDPGFX_H264_QUANT_QUALITY>& {
  return quality;
}
auto Regions::Bounds() const -> Rect {
  return bounds;
}
auto Regions::Clear() -> void {
  areas.clear();
  quality.clear();
}
}
