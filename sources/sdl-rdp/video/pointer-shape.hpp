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
  auto Send(rdpContext& context) const -> PointerDelivery;

private:
  auto SendLarge(rdpContext& context) const -> BOOL;
  auto ColorImage() const                   -> POINTER_COLOR_UPDATE;
  auto LargeImage() const                   -> POINTER_LARGE_UPDATE;
  Extent            _size;
  unsigned          _hot_x { };
  unsigned          _hot_y { };
  std::vector<BYTE> _pixels;
  std::vector<BYTE> _mask;
};
}
