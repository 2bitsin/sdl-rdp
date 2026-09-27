#pragma once
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/freerdp-facade/planar-encoder.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/freerdp-facade/surface-encoder.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/video/scaler.hpp>

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace sdl_rdp::video::detail::encoder {
using sdl_rdp::configuration::Codec;
using sdl_rdp::freerdp_facade::PlanarEncoder;
using sdl_rdp::freerdp_facade::SettingsReader;
using sdl_rdp::freerdp_facade::SurfaceEncoder;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::RowBytes;
using sdl_rdp::utilities::Rows;

class Encoder {
public:
  auto SetupPlanar(SettingsReader settings, bool xrgb = false) -> void;
  auto Select(SettingsReader settings, Codec preference)       -> void;
  auto Encode(std::span<std::uint8_t const> pixels, std::uint32_t width, std::uint32_t height) -> bool;
  auto Id(SettingsReader settings) const                       -> std::uint32_t;
  auto SelectedCodec() const noexcept                          -> Codec;
  auto Use(Codec value) noexcept                               -> void;
  auto Payload() const noexcept                                -> std::span<std::byte const>;
  auto EncodeTime() const noexcept                             -> std::chrono::nanoseconds;
  auto Charge(std::chrono::nanoseconds elapsed) noexcept       -> void;

private:
  auto Prepare(SettingsReader settings)                           -> void;
  auto Encoded(std::span<std::uint8_t const> pixels, Extent size) -> std::optional<std::span<std::byte const>>;
  Codec                         codec      { Codec::Raw };
  std::chrono::nanoseconds      encode_time{ };
  std::span<std::byte const>    payload;
  std::optional<PlanarEncoder>  planar;
  std::optional<SurfaceEncoder> remote_fx;
  std::optional<SurfaceEncoder> nsc;
};
template <std::predicate<Rect, std::span<std::byte const>> Consume>
auto EncodePlanarRows(Encoder& encoder, Scaler& scaler, Rect area, Consume consume) -> bool {
  std::vector<std::uint8_t> scratch(RowBytes(area.w));
  return std::ranges::all_of(Rows(area), [&](Rect row) {
    auto const pixels = scaler.Copy(row, scratch, RowOrder::TopDown);
    return encoder.Encode(pixels.pixels, row.w, 1) && consume(row, encoder.Payload());
  });
}
}

namespace sdl_rdp::video {
using detail::encoder::EncodePlanarRows;
using detail::encoder::Encoder;
}
