#pragma once
#include <sdl-rdp/utilities/geometry.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace sdl_rdp::video::avc::detail::regions {
using sdl_rdp::utilities::Rect;
// MS-RDPEGFX 2.2.4.4.1 RFX_AVC420_METABLOCK: an exclusive-corner rectangle, and its quantization and quality.
struct WireRect {
  std::uint16_t left  { };
  std::uint16_t top   { };
  std::uint16_t right { };
  std::uint16_t bottom{ };
};
struct QuantQuality {
  std::uint8_t qp_value     { };
  std::uint8_t quality_value{ };
  std::uint8_t qp           { };
  std::uint8_t r            { };
  std::uint8_t p            { };
};
class Regions {
public:
  auto Add(Rect area) -> void;
  auto Bytes() const  -> std::size_t;
  auto Areas()        -> std::span<WireRect>;
  auto Quality()      -> std::span<QuantQuality>;
  auto Bounds() const -> Rect;
  auto Clear()        -> void;

private:
  std::vector<WireRect>     areas;
  std::vector<QuantQuality> quality;
  Rect                      bounds { };
};
}

namespace sdl_rdp::video::avc {
using detail::regions::QuantQuality;
using detail::regions::Regions;
using detail::regions::WireRect;
}
