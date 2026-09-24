#pragma once
#include <sdl-rdp/utilities/extent.hpp>

#include <freerdp/pointer.h>
#include <span>
#include <vector>

namespace Backend {
inline constexpr unsigned LargePointerLimit = 384;
enum class PointerDelivery{ Sent, Failed, Unsupported };
class PointerShape {
public:
       PointerShape() noexcept = default;
       PointerShape(Extent size, unsigned x, unsigned y, std::span<BYTE const> argb);
  auto Send(rdpContext& context) -> PointerDelivery;

private:
  auto SendLarge(rdpContext& context) -> BOOL;
  Extent            _size;
  unsigned          _hot_x { };
  unsigned          _hot_y { };
  std::vector<BYTE> _pixels;
  std::vector<BYTE> _mask;
};
}
