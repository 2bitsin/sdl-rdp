#pragma once
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace sdl_rdp::freerdp_facade::detail::planar_encoder {
using sdl_rdp::utilities::Pinned;

struct PlanarOptions {
  bool skip_alpha   { };
  bool dynamic_color{ };
};
// Skipping alpha or not is fixed per FreeRDP planar context, so changing it starts a new one.
class PlanarEncoder : private Pinned {
public:
  explicit PlanarEncoder(PlanarOptions options);
           ~PlanarEncoder();
  auto     Configure(PlanarOptions options) -> void;
  // One BGRA row; the bitmap lives until the next Encode.
  auto Encode(std::span<std::uint8_t const> row) -> std::optional<std::span<std::byte const>>;

private:
  struct State;
  auto Grow(std::uint32_t width)                                        -> bool;
  auto Fallback(std::span<std::uint8_t const> row, std::uint32_t& size) -> bool;
  std::unique_ptr<State> _state;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::planar_encoder::PlanarEncoder;
using detail::planar_encoder::PlanarOptions;
}
