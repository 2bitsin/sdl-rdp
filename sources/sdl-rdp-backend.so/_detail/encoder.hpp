#pragma once
#include "sdl-rdp-backend.h"
#include "rdp-handles.hpp"
#include <freerdp/settings.h>
#include <freerdp/codec/planar.h>
#include <freerdp/codec/rfx.h>
#include <freerdp/codec/nsc.h>
#include <span>
#include <chrono>
#include <vector>

namespace Backend {
struct Encoder {
public:
  bool                                                                                 SetupPlanar(rdpSettings const* settings, bool xrgb = false);
  bool                                                                                 Select(rdpSettings const* settings, sdlrdp_codec preference);
  bool                                                                                 EncodePlanar(std::span<BYTE const> pixels, unsigned width);
  bool                                                                                 Encode(std::span<BYTE const> pixels, unsigned width, unsigned height);
  bool                                                                                 EncodePayload(std::span<BYTE const> pixels, unsigned width, unsigned height);
  unsigned                                                                             Id(rdpSettings const* settings) const;
  sdlrdp_codec                                                                                 codec        = SDLRDP_CODEC_RAW;
  unsigned                                                                                     planar_width = 0, rfx_width = 0, rfx_height = 0;
  bool                                                                                         skip_alpha   = false, dynamic_color = false;
  std::chrono::nanoseconds                                                                     encode_time { };
  std::span<BYTE>                                                                              payload;
  std::unique_ptr<BITMAP_PLANAR_CONTEXT, Releases<freerdp_bitmap_planar_context_free>> planar, plain;
  std::unique_ptr<RFX_CONTEXT, Releases<rfx_context_free>>                                     rfx;
  std::unique_ptr<NSC_CONTEXT, Releases<nsc_context_free>>                                     nsc;
  std::unique_ptr<wStream, ReleaseStream>                                                      stream;
  std::vector<BYTE> compressed,                                                                scratch;
};
}
