#pragma once
#include <sdl-rdp/utilities/extent.hpp>

#include <freerdp/pointer.h>
#include <cstdint>
#include <span>
#include <vector>

namespace Backend {
inline constexpr std::uint32_t LargePointerLimit = 384;
enum class PointerDelivery{ Sent, Failed, Unsupported };
class PointerShape {
  // FreeRDP's pointer update structs take the buffers as non-const pointers, so each send lends copies.
  struct Buffers {
    std::vector<std::uint8_t> pixels;
    std::vector<std::uint8_t> mask;
  };

public:
       PointerShape() noexcept = default;
       PointerShape(Extent size, std::uint32_t x, std::uint32_t y, std::span<std::uint8_t const> argb);
  auto Send(rdpContext& context) const -> PointerDelivery;

private:
  auto SendLarge(rdpContext& context, Buffers& buffers) const -> bool;
  auto ColorImage(Buffers& buffers) const                     -> POINTER_COLOR_UPDATE;
  auto LargeImage(Buffers& buffers) const                     -> POINTER_LARGE_UPDATE;
  Extent                    _size;
  std::uint32_t             _hot_x { };
  std::uint32_t             _hot_y { };
  std::vector<std::uint8_t> _pixels;
  std::vector<std::uint8_t> _mask;
};
}
