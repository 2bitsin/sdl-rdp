#pragma once
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace sdl_rdp::freerdp_facade::detail::progressive_encoder {
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Rect;

// Owns a progressive context, which keeps the tiles of the surface it last compressed.
class ProgressiveEncoder : private Pinned {
public:
  ProgressiveEncoder();
  ~ProgressiveEncoder();
  // The message lives in the context until the next Compress.
  auto Compress(std::span<std::uint8_t const> bgrx, std::uint32_t stride, Extent size, std::span<Rect const> damage)
      -> std::optional<std::span<std::byte const>>;

private:
  struct State;
  std::unique_ptr<State> _state;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::progressive_encoder::ProgressiveEncoder;
}
