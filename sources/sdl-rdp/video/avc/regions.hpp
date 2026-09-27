#pragma once
#include <sdl-rdp/freerdp-facade/graphics-channel.hpp>
#include <sdl-rdp/utilities/geometry.hpp>

#include <cstddef>
#include <vector>

namespace sdl_rdp::video::avc::detail::regions {
using sdl_rdp::freerdp_facade::Avc420Metablock;
using sdl_rdp::utilities::Rect;
// The damaged areas of an AVC420 frame, each at the one quality every region is encoded at.
class Regions {
public:
  auto Add(Rect area)    -> void;
  auto Bytes() const     -> std::size_t;
  auto Metablock() const -> Avc420Metablock;
  auto Bounds() const    -> Rect;
  auto Clear()           -> void;

private:
  std::vector<Rect> areas;
  Rect              bounds{ };
};
}

namespace sdl_rdp::video::avc {
using detail::regions::Regions;
}
