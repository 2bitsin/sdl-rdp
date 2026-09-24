#pragma once
#include "extent.hpp"
#include "rdp-handles.hpp"
#include "release-stream.hpp"
#include "sdl-rdp-backend.h"

#include <chrono>
#include <freerdp/codec/nsc.h>
#include <freerdp/codec/planar.h>
#include <freerdp/codec/rfx.h>
#include <freerdp/settings.h>
#include <span>
#include <vector>

namespace Backend {
using RemoteFxContext = std::unique_ptr<RFX_CONTEXT, Releases<rfx_context_free>>;
using NsCodecContext  = std::unique_ptr<NSC_CONTEXT, Releases<nsc_context_free>>;
class Encoder {
public:
  bool                     SetupPlanar(rdpSettings const* settings, bool xrgb = false);
  bool                     Select(rdpSettings const* settings, sdlrdp_codec preference);
  bool                     EncodePlanar(std::span<BYTE const> pixels, unsigned width);
  bool                     Encode(std::span<BYTE const> pixels, unsigned width, unsigned height);
  bool                     EncodePayload(std::span<BYTE const> pixels, unsigned width, unsigned height);
  unsigned                 Id(rdpSettings const* settings) const;
  sdlrdp_codec             Codec() const                            noexcept;
  void                     Use(sdlrdp_codec value)                  noexcept;
  std::span<BYTE const>    Payload() const                          noexcept;
  std::chrono::nanoseconds EncodeTime() const                       noexcept;
  void                     Charge(std::chrono::nanoseconds elapsed) noexcept;
  std::span<BYTE>          Scratch(std::size_t size);

private:
  bool EncodeRemoteFx(std::span<BYTE const> pixels, unsigned width, unsigned height);
  bool InitializeCodec(rdpSettings const* settings);
  auto ResetRemoteFx(unsigned width, unsigned height) -> bool;
  using PlanarContext = std::unique_ptr<BITMAP_PLANAR_CONTEXT, Releases<freerdp_bitmap_planar_context_free>>;
  struct PlanarState {
    PlanarContext        context;
    PlanarContext        fallback;
    std::vector<uint8_t> compressed;
    unsigned             width        { };
    bool                 skip_alpha   { };
    bool                 dynamic_color{ };
  };
  struct RemoteFxState {
    RemoteFxContext context;
    Extent          size;
  };
  sdlrdp_codec                            codec      { SDLRDP_CODEC_RAW };
  std::chrono::nanoseconds                encode_time{ };
  std::span<uint8_t>                      payload;
  PlanarState                             planar;
  RemoteFxState                           remote_fx;
  NsCodecContext                          nsc;
  std::unique_ptr<wStream, ReleaseStream> stream;
  std::vector<uint8_t>                    scratch;
};
}
