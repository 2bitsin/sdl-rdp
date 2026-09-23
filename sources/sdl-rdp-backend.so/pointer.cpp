#include "_detail/state.hpp"

#include <cstddef>
#include <cstring>
#include <freerdp/pointer.h>

namespace Backend {
void State::SetPointer(unsigned w, unsigned h, unsigned x, unsigned y, void const* pixels) {
  Pointer next   { .width = w, .height = h, .hot_x = x, .hot_y = y };
  auto    stride = ((w + 15) / 16) * 2;
  next.mask.resize(static_cast<std::size_t>(stride) * h);
  next.pixels.resize(static_cast<std::size_t>(w * h) * 4);
  for (unsigned row = 0; row < h; ++row) {
    auto const* source      = static_cast<BYTE const*>(pixels) + (static_cast<std::size_t>(row * w) * 4);
    auto*       destination = next.pixels.data() + (static_cast<std::size_t>((h - row - 1) * w) * 4);
    std::memcpy(destination, source, static_cast<std::size_t>(w) * 4);
    for (unsigned column = 0; column < w; ++column)
      if (!source[(column * 4) + 3]) next.mask[((h - row - 1) * stride) + (column / 8)] |= 0x80 >> (column % 8);
  }
  std::scoped_lock const lock(session_guard, peers_guard);
  pointer = std::move(next);
  ++pointer_generation;
  for (auto const& peer : peers)
    if (peer->active) peer->wake.Transition(WakeEvent::Phase::Pending);
}
namespace {
bool LargePointer(Peer& peer, Pointer& shape) {
  auto* context = peer.client->context;
  auto* update  = context->update->pointer;
  if (!(freerdp_settings_get_uint32(context->settings, FreeRDP_LargePointerFlag) & LARGE_POINTER_FLAG_384x384)) {
    peer.owner.Log(SDLRDP_LOG_WARN, "Client does not support a 384x384 pointer.");
    peer.pointer_generation = peer.owner.pointer_generation;
    return true;
  }
  POINTER_LARGE_UPDATE const image{ 32,
                                    0,
                                    UINT16(shape.hot_x),
                                    UINT16(shape.hot_y),
                                    UINT16(shape.width),
                                    UINT16(shape.height),
                                    UINT32(shape.mask.size()),
                                    UINT32(shape.pixels.size()),
                                    shape.pixels.data(),
                                    shape.mask.data() };
  return update->PointerLarge(context, &image);
}
}
bool Peer::SendPointer() {
  if (pointer_generation == owner.pointer_generation) return true;
  auto& shape   = owner.pointer;
  auto* context = client->context;
  auto* update  = context->update->pointer;
  bool  result  = false;
  if (!shape.width) {
    POINTER_SYSTEM_UPDATE const hidden { SYSPTR_NULL };
    result = update->PointerSystem(context, &hidden);
  } else if (shape.width <= 96 && shape.height <= 96) {
    POINTER_NEW_UPDATE const image{ 32,
                                    { 0, UINT16(shape.hot_x), UINT16(shape.hot_y), UINT16(shape.width),
                                      UINT16(shape.height), UINT16(shape.mask.size()), UINT16(shape.pixels.size()),
                                      shape.pixels.data(), shape.mask.data() } };
    result = update->PointerNew(context, &image);
  } else {
    result = LargePointer(*this, shape);
  }
  if (result) pointer_generation = owner.pointer_generation;
  return result;
}
} // namespace Backend
