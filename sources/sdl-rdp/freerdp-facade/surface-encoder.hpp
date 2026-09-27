#pragma once
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>

namespace sdl_rdp::freerdp_facade::detail::surface_encoder {
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Pinned;

enum class SurfaceCodec : std::uint8_t { RemoteFx, NsCodec };
// Owns one codec's context and the stream its messages compose into.
class SurfaceEncoder : private Pinned {
public:
  explicit SurfaceEncoder(SurfaceCodec codec);
           ~SurfaceEncoder();
  // The message lives until the next Encode.
  auto Encode(std::span<std::uint8_t const> bgrx, Extent size) -> std::optional<std::span<std::byte const>>;

private:
  struct State;
  std::unique_ptr<State> _state;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::surface_encoder::SurfaceCodec;
using detail::surface_encoder::SurfaceEncoder;
}
