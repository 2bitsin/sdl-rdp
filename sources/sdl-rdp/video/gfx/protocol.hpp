#pragma once
#include <sdl-rdp/freerdp-facade/graphics-channel-events.hpp>

#include <algorithm>
#include <array>
#include <optional>
#include <ranges>
#include <span>

namespace sdl_rdp::video::gfx::detail::protocol {
using sdl_rdp::freerdp_facade::GfxCapability;
using sdl_rdp::freerdp_facade::GfxCapsFlags;
using sdl_rdp::freerdp_facade::GfxVersion;
using sdl_rdp::freerdp_facade::Has;

inline constexpr std::array versions{ GfxVersion::V8, GfxVersion::V81, GfxVersion::V10, GfxVersion::V101,
                                      GfxVersion::V102, GfxVersion::V103, GfxVersion::V104, GfxVersion::V105,
                                      GfxVersion::V106, GfxVersion::V106Err, GfxVersion::V107 };
inline auto AllowsAvc(GfxCapability cap) -> bool {
  return cap.version == GfxVersion::V81 ? Has(cap.flags, GfxCapsFlags::Avc420Enabled)
                                        : cap.version >= GfxVersion::V10 && !Has(cap.flags, GfxCapsFlags::AvcDisabled);
}
inline auto Acceptable(GfxCapability cap) -> bool {
  return std::ranges::contains(versions, cap.version);
}
inline auto Newest(std::span<GfxCapability const> caps) -> std::optional<GfxCapability> {
  auto       acceptable = caps | std::views::filter(Acceptable);
  auto const newest     = std::ranges::max_element(acceptable, { }, &GfxCapability::version);
  return newest == acceptable.end() ? std::nullopt : std::optional{ *newest };
}
// MS-RDPEGFX 2.2.3.4: version 10.1 carries reserved bytes in place of flags.
inline auto AnsweredFlags(GfxCapability cap, bool avc) -> GfxCapsFlags {
  if (cap.version == GfxVersion::V101) return GfxCapsFlags{ };
  auto const kept = cap.flags & (GfxCapsFlags::ThinClient | GfxCapsFlags::SmallCache | GfxCapsFlags::ScaledMapDisable);
  if (cap.version == GfxVersion::V81) return avc ? kept | GfxCapsFlags::Avc420Enabled : kept;
  return cap.version >= GfxVersion::V10 && !avc ? kept | GfxCapsFlags::AvcDisabled : kept;
}
inline auto SelectCapability(std::span<GfxCapability const> caps, bool avc_available = false)
    -> std::optional<GfxCapability> {
  return Newest(caps).transform([avc_available](GfxCapability selected) {
    return GfxCapability{ .version = selected.version,
                          .flags   = AnsweredFlags(selected, avc_available && AllowsAvc(selected)) };
  });
}
}

namespace sdl_rdp::video::gfx {
using detail::protocol::AllowsAvc;
using detail::protocol::SelectCapability;
}
