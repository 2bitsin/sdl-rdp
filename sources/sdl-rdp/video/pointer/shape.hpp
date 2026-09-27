#pragma once
#include <sdl-rdp/freerdp-facade/updates.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/video/pointer/layout.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace sdl_rdp::video::pointer::detail::shape {
using sdl_rdp::freerdp_facade::Connection;
using sdl_rdp::freerdp_facade::PointerImage;
using sdl_rdp::utilities::Extent;

enum class PointerDelivery{ Sent, Failed, Unsupported };
class PointerShape {
public:
       PointerShape() noexcept = default;
       PointerShape(PointerLayout const& layout, std::span<std::uint8_t const> argb);
  auto Send(Connection& connection) const -> PointerDelivery;

private:
  auto Image() const noexcept -> PointerImage;
  Extent                    _size;
  std::uint32_t             _hot_x { };
  std::uint32_t             _hot_y { };
  std::vector<std::uint8_t> _pixels;
  std::vector<std::uint8_t> _mask;
};
}

namespace sdl_rdp::video::pointer {
using detail::shape::PointerDelivery;
using detail::shape::PointerShape;
}
