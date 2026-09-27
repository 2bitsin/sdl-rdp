#include <sdl-rdp/video/avc/regions.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace sdl_rdp::video::avc::detail::regions {
using sdl_rdp::utilities::Expects;
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
  areas.push_back(area);
}
// MS-RDPEGFX 2.2.4.4.1: the region count, then eight bytes of corners and two of quality per region.
auto Regions::Bytes() const -> std::size_t {
  return 4 + (10 * areas.size());
}
auto Regions::Metablock() const -> Avc420Metablock {
  return { .regions = areas, .quality = { .qp = 26, .progressive = true, .quality = 100 } };
}
auto Regions::Bounds() const -> Rect {
  return bounds;
}
auto Regions::Clear() -> void {
  areas.clear();
}
}
