#pragma once
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/video/scaler.hpp>

#include <freerdp/codec/nsc.h>
#include <freerdp/codec/planar.h>
#include <freerdp/codec/rfx.h>
#include <winpr/stream.h>
#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace sdl_rdp::video::detail::encoder {
using sdl_rdp::configuration::Codec;
using sdl_rdp::freerdp_facade::SettingsReader;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::Releases;
using sdl_rdp::utilities::RowBytes;
using sdl_rdp::utilities::Rows;

// abi: release steps no single FreeRDP free function performs as a plain call.
auto FreeStream(wStream* stream) noexcept -> void;
using StreamHandle    = std::unique_ptr<wStream, Releases<FreeStream>>;
using RemoteFxContext = std::unique_ptr<RFX_CONTEXT, Releases<rfx_context_free>>;
using NsCodecContext  = std::unique_ptr<NSC_CONTEXT, Releases<nsc_context_free>>;
class Encoder {
public:
  auto SetupPlanar(SettingsReader settings, bool xrgb = false)                 -> bool;
  auto Select(SettingsReader settings, Codec preference)                       -> bool;
  auto EncodePlanar(std::span<std::uint8_t const> pixels, std::uint32_t width) -> bool;
  auto Encode(std::span<std::uint8_t const> pixels, std::uint32_t width, std::uint32_t height) -> bool;
  auto EncodePayload(std::span<std::uint8_t const> pixels, std::uint32_t width, std::uint32_t height) -> bool;
  auto Id(SettingsReader settings) const                                       -> std::uint32_t;
  auto SelectedCodec() const noexcept                                          -> Codec;
  auto Use(Codec value) noexcept                                               -> void;
  auto Payload() const noexcept                                                -> std::span<std::byte const>;
  auto EncodeTime() const noexcept                                             -> std::chrono::nanoseconds;
  auto Charge(std::chrono::nanoseconds elapsed) noexcept                       -> void;

private:
  auto EncodeRemoteFx(std::span<std::uint8_t const> pixels, std::uint32_t width, std::uint32_t height) -> bool;
  auto InitializeCodec(SettingsReader settings)                                                        -> bool;
  auto ResetRemoteFx(std::uint32_t width, std::uint32_t height)                                        -> bool;
  using PlanarContext = std::unique_ptr<BITMAP_PLANAR_CONTEXT, Releases<freerdp_bitmap_planar_context_free>>;
  struct PlanarState {
    PlanarContext          context;
    PlanarContext          fallback;
    std::vector<std::byte> compressed;
    std::uint32_t          width        { };
    bool                   skip_alpha   { };
    bool                   dynamic_color{ };
  };
  struct RemoteFxState {
    RemoteFxContext context;
    Extent          size;
  };
  Codec                    codec      { Codec::Raw };
  std::chrono::nanoseconds encode_time{ };
  std::span<std::byte>     payload;
  PlanarState              planar;
  RemoteFxState            remote_fx;
  NsCodecContext           nsc;
  StreamHandle             stream;
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
