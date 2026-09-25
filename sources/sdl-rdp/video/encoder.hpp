#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/release-stream.hpp>
#include <sdl-rdp/utilities/extent.hpp>

#include <freerdp/codec/nsc.h>
#include <freerdp/codec/planar.h>
#include <freerdp/codec/rfx.h>
#include <freerdp/settings.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Backend {
using RemoteFxContext = std::unique_ptr<RFX_CONTEXT, Releases<rfx_context_free>>;
using NsCodecContext  = std::unique_ptr<NSC_CONTEXT, Releases<nsc_context_free>>;
class Encoder {
public:
  auto SetupPlanar(rdpSettings const* settings, bool xrgb = false)             -> bool;
  auto Select(rdpSettings const* settings, sdlrdp_codec preference)            -> bool;
  auto EncodePlanar(std::span<std::uint8_t const> pixels, std::uint32_t width) -> bool;
  auto Encode(std::span<std::uint8_t const> pixels, std::uint32_t width, std::uint32_t height) -> bool;
  auto EncodePayload(std::span<std::uint8_t const> pixels, std::uint32_t width, std::uint32_t height) -> bool;
  auto Id(rdpSettings const* settings) const                                   -> std::uint32_t;
  auto Codec() const noexcept                                                  -> sdlrdp_codec;
  auto Use(sdlrdp_codec value) noexcept                                        -> void;
  auto Payload() const noexcept                                                -> std::span<std::byte const>;
  auto EncodeTime() const noexcept                                             -> std::chrono::nanoseconds;
  auto Charge(std::chrono::nanoseconds elapsed) noexcept                       -> void;

private:
  auto EncodeRemoteFx(std::span<std::uint8_t const> pixels, std::uint32_t width, std::uint32_t height) -> bool;
  auto InitializeCodec(rdpSettings const* settings)                                                    -> bool;
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
  sdlrdp_codec                            codec      { SDLRDP_CODEC_RAW };
  std::chrono::nanoseconds                encode_time{ };
  std::span<std::byte>                    payload;
  PlanarState                             planar;
  RemoteFxState                           remote_fx;
  NsCodecContext                          nsc;
  std::unique_ptr<wStream, ReleaseStream> stream;
};
}
