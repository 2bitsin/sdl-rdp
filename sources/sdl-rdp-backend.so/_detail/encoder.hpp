#pragma once
#include "rdp-handles.hpp"
#include "sdl-rdp-backend.h"

#include <chrono>
#include <freerdp/codec/nsc.h>
#include <freerdp/codec/planar.h>
#include <freerdp/codec/rfx.h>
#include <freerdp/settings.h>
#include <span>
#include <vector>

namespace Backend {
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
  bool ResetRemoteFx(unsigned width, unsigned height);
  using PlanarContext = std::unique_ptr<BITMAP_PLANAR_CONTEXT, Releases<freerdp_bitmap_planar_context_free>>;
  sdlrdp_codec                                             codec        { SDLRDP_CODEC_RAW };
  unsigned                                                 planar_width { };
  unsigned                                                 rfx_width    { };
  unsigned                                                 rfx_height   { };
  bool                                                     skip_alpha   { };
  bool                                                     dynamic_color{ };
  std::chrono::nanoseconds                                 encode_time  { };
  std::span<BYTE>                                          payload;
  PlanarContext                                            planar;
  PlanarContext                                            plain;
  std::unique_ptr<RFX_CONTEXT, Releases<rfx_context_free>> rfx;
  std::unique_ptr<NSC_CONTEXT, Releases<nsc_context_free>> nsc;
  std::unique_ptr<wStream, ReleaseStream>                  stream;
  std::vector<BYTE>                                        compressed;
  std::vector<BYTE>                                        scratch;
};
}
