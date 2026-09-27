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
  areas.push_back({ .left   = Narrowed<std::uint16_t>(area.x),
                    .top    = Narrowed<std::uint16_t>(area.y),
                    .right  = Narrowed<std::uint16_t>(area.x + area.w),
                    .bottom = Narrowed<std::uint16_t>(area.y + area.h) });
  quality.push_back({ .qp_value = 0x9a, .quality_value = 100, .qp = 26, .r = 0, .p = 1 });
}
auto Regions::Bytes() const -> std::size_t {
  Expects(areas.size() == quality.size(), "every region has quantization metadata");
  return 4 + (10 * areas.size());
}
auto Regions::Areas() -> std::span<WireRect> {
  return areas;
}
auto Regions::Quality() -> std::span<QuantQuality> {
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
