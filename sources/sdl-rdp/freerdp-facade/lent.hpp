#pragma once
#include <oxbox/utilities/span.hpp>
#include <cstdint>
#include <span>

namespace sdl_rdp::freerdp_facade::detail::lent {
// abi: FreeRDP only reads BYTE* payload fields (3.32 surface.c:317, update.c:339,2433-2484, rdpgfx_main.c:711,760,834).
template <class ElementTy> auto Lent(std::span<ElementTy const> bytes) -> std::span<std::uint8_t> {
  auto const view = oxbox::utilities::SpanCast<std::uint8_t const>(bytes);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast): the abi line above, written once for updates and rdpgfx.
  return { const_cast<std::uint8_t*>(view.data()), view.size() };
}
}

namespace sdl_rdp::freerdp_facade {
using detail::lent::Lent;
}
