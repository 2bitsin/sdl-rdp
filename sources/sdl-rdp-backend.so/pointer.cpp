#include "_detail/state.hpp"
#include <freerdp/pointer.h>
#include <cstring>

namespace Backend {
void State::SetPointer(unsigned w, unsigned h, unsigned x, unsigned y, void const* pixels)
{
  Pointer next{w, h, x, y};
  auto stride = ((w + 15) / 16) * 2;
  next.mask.resize(stride * h);
  next.pixels.resize(w * h * 4);
  for (unsigned row = 0; row < h; ++row) {
    auto source = static_cast<BYTE const*>(pixels) + row * w * 4;
    auto destination = next.pixels.data() + (h - row - 1) * w * 4;
    std::memcpy(destination, source, w * 4);
    for (unsigned column = 0; column < w; ++column)
      if (!source[column * 4 + 3]) next.mask[(h - row - 1) * stride + column / 8] |= 0x80 >> (column % 8);
  }
  std::scoped_lock lock(session_guard, peers_guard);
  pointer = std::move(next);
  ++pointer_generation;
  for (auto const& peer : peers) if (peer->active) peer->wake.Transition(WakeEvent::Phase::Pending);
}
bool Peer::SendPointer()
{
  if (pointer_generation == owner.pointer_generation) return true;
  auto& shape = owner.pointer;
  auto context = client->context;
  auto update = context->update->pointer;
  bool result;
  if (!shape.width) {
    POINTER_SYSTEM_UPDATE hidden{SYSPTR_NULL};
    result = update->PointerSystem(context, &hidden);
  } else if (shape.width <= 96 && shape.height <= 96) {
    POINTER_NEW_UPDATE image{32, {0, UINT16(shape.hot_x), UINT16(shape.hot_y), UINT16(shape.width), UINT16(shape.height),
      UINT16(shape.mask.size()), UINT16(shape.pixels.size()), shape.pixels.data(), shape.mask.data()}};
    result = update->PointerNew(context, &image);
  } else {
    if (!(freerdp_settings_get_uint32(context->settings, FreeRDP_LargePointerFlag) & LARGE_POINTER_FLAG_384x384)) {
      owner.Log(SDLRDP_LOG_WARN, "Client does not support a 384x384 pointer.");
      pointer_generation = owner.pointer_generation;
      return true;
    }
    POINTER_LARGE_UPDATE image{32, 0, UINT16(shape.hot_x), UINT16(shape.hot_y), UINT16(shape.width), UINT16(shape.height),
      UINT32(shape.mask.size()), UINT32(shape.pixels.size()), shape.pixels.data(), shape.mask.data()};
    result = update->PointerLarge(context, &image);
  }
  if (result) pointer_generation = owner.pointer_generation;
  return result;
}
}
