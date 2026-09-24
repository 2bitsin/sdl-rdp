#include "_detail/encoder.hpp"

#include "_detail/contract.hpp"
#include "_detail/extent.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <freerdp/constants.h>

namespace Backend {
namespace {
constexpr std::size_t InitialStreamCapacity = 64uz * 1024;
bool PrepareRemoteFx(std::unique_ptr<RFX_CONTEXT, Releases<rfx_context_free>>& rfx) {
  if (!rfx) rfx.reset(rfx_context_new_ex(TRUE, THREADING_FLAGS_DISABLE_THREADS));
  if (rfx) rfx_context_set_pixel_format(rfx.get(), PIXEL_FORMAT_BGRX32);
  return bool(rfx);
}
bool PrepareNsCodec(std::unique_ptr<NSC_CONTEXT, Releases<nsc_context_free>>& nsc) {
  if (!nsc) nsc.reset(nsc_context_new());
  return nsc && nsc_context_set_parameters(nsc.get(), NSC_COLOR_FORMAT, PIXEL_FORMAT_BGRX32) &&
         nsc_context_set_parameters(nsc.get(), NSC_COLOR_LOSS_LEVEL, 1) &&
         nsc_context_set_parameters(nsc.get(), NSC_ALLOW_SUBSAMPLING, 0);
}
BYTE* CompressRow(BITMAP_PLANAR_CONTEXT& context, std::span<BYTE const> pixels, unsigned width, std::span<BYTE> out,
                  UINT32& size) {
  return freerdp_bitmap_compress_planar(&context, pixels.data(), PIXEL_FORMAT_BGRA32, width, 1, width * PixelBytes,
                                        out.data(), &size);
}
bool Available(rdpSettings const* settings, sdlrdp_codec codec) {
  utilities::Expects(settings != nullptr, "negotiated settings exist");
  auto surface = freerdp_settings_get_bool(settings, FreeRDP_SurfaceCommandsEnabled);
  switch (codec) {
  case SDLRDP_CODEC_PLANAR:
    return freerdp_settings_get_uint32(settings, FreeRDP_ColorDepth) == 32;
  case SDLRDP_CODEC_REMOTEFX:
    return surface && freerdp_settings_get_bool(settings, FreeRDP_RemoteFxCodec);
  case SDLRDP_CODEC_NSCODEC:
    return surface && freerdp_settings_get_bool(settings, FreeRDP_NSCodec);
  case SDLRDP_CODEC_RAW:
    return true;
  case SDLRDP_CODEC_AVC420:
  case SDLRDP_CODEC_PROGRESSIVE:
  case SDLRDP_CODEC_AUTO:
    return false;
  default:
    utilities::Unreachable(codec);
  }
}
}
bool Encoder::SetupPlanar(rdpSettings const* settings, bool xrgb) {
  utilities::Expects(settings != nullptr, "negotiated settings exist");
  auto alpha = xrgb || freerdp_settings_get_bool(settings, FreeRDP_DrawAllowSkipAlpha);
  if (skip_alpha != alpha) {
    planar.reset();
    planar_width = 0;
  }
  skip_alpha    = alpha;
  dynamic_color = freerdp_settings_get_bool(settings, FreeRDP_DrawAllowDynamicColorFidelity);
  if (!stream) stream.reset(Stream_New(nullptr, InitialStreamCapacity));
  if (!planar)
    planar.reset(
        freerdp_bitmap_planar_context_new(PLANAR_FORMAT_HEADER_RLE | (skip_alpha ? PLANAR_FORMAT_HEADER_NA : 0), 1, 1));
  return stream && planar;
}
bool Encoder::InitializeCodec(rdpSettings const* settings) {
  switch (codec) {
  case SDLRDP_CODEC_PLANAR:
    return SetupPlanar(settings);
  case SDLRDP_CODEC_REMOTEFX:
    return PrepareRemoteFx(rfx);
  case SDLRDP_CODEC_NSCODEC:
    return PrepareNsCodec(nsc);
  case SDLRDP_CODEC_RAW:
    return true;
  default:
    utilities::Unreachable(codec);
  }
}
bool Encoder::Select(rdpSettings const* settings, sdlrdp_codec preference) {
  utilities::Expects(settings != nullptr, "negotiated settings exist");
  if (freerdp_settings_get_uint32(settings, FreeRDP_ColorDepth) != 32) preference = SDLRDP_CODEC_RAW;
  constexpr std::array choices{ SDLRDP_CODEC_REMOTEFX, SDLRDP_CODEC_NSCODEC, SDLRDP_CODEC_PLANAR, SDLRDP_CODEC_RAW };
  codec = Available(settings, preference)
              ? preference
              : *std::ranges::find_if(choices, [=](auto choice) { return Available(settings, choice); });
  if (!stream) stream.reset(Stream_New(nullptr, InitialStreamCapacity));
  if (!stream) return false;
  return InitializeCodec(settings);
}
bool Encoder::Encode(std::span<BYTE const> pixels, unsigned width, unsigned height) {
  auto start  = std::chrono::steady_clock::now();
  auto result = EncodePayload(pixels, width, height);
  Charge(std::chrono::steady_clock::now() - start);
  return result;
}
bool Encoder::ResetRemoteFx(unsigned width, unsigned height) {
  if (width != rfx_width || height != rfx_height) {
    if (!rfx_context_reset(rfx.get(), width, height)) return false;
    rfx_width  = width;
    rfx_height = height;
  }
  return true;
}
bool Encoder::EncodeRemoteFx(std::span<BYTE const> pixels, unsigned width, unsigned height) {
  if (!ResetRemoteFx(width, height)) return false;
  RFX_RECT const rect{ 0, 0, UINT16(width), UINT16(height) };
  return rfx_compose_message(rfx.get(), stream.get(), &rect, 1, pixels.data(), width, height, width * 4);
}
bool Encoder::EncodePayload(std::span<BYTE const> pixels, unsigned width, unsigned height) {
  utilities::Expects(width, "encoder input is a packed band");
  utilities::Expects(height, "encoder input is a packed band");
  utilities::Expects(pixels.size() == std::size_t(width) * height * 4, "encoder input is a packed band");
  Stream_SetPosition(stream.get(), 0);
  if (codec == SDLRDP_CODEC_PLANAR) {
    utilities::Expects(height == 1, "planar avoids signed delta corruption in FreeRDP 3.15");
    return EncodePlanar(pixels, width);
  }
  bool result = false;
  if (codec == SDLRDP_CODEC_REMOTEFX) {
    result = EncodeRemoteFx(pixels, width, height);
  } else if (codec == SDLRDP_CODEC_NSCODEC)
    result = nsc_compose_message(nsc.get(), stream.get(), pixels.data(), width, height, width * 4);
  else
    utilities::Unreachable(codec);
  payload = { Stream_Buffer(stream.get()), Stream_GetPosition(stream.get()) };
  return result;
}
bool Encoder::EncodePlanar(std::span<BYTE const> pixels, unsigned width) {
  utilities::Expects(planar != nullptr, "planar context exists");
  utilities::Expects(pixels.size() == std::size_t{ width } * PixelBytes, "planar input is one row");
  if (width > planar_width) {
    if (!freerdp_bitmap_planar_context_reset(planar.get(), width, 1)) return false;
    planar_width = width;
  }
  compressed.resize(pixels.size() + 1024);
  UINT32 size   = compressed.size();
  auto*  result = width < 4 ? nullptr : CompressRow(*planar, pixels, width, compressed, size);
  if (!result) {
    plain.reset(freerdp_bitmap_planar_context_new(skip_alpha ? PLANAR_FORMAT_HEADER_NA : 0, width, 1));
    if (!plain) return false;
    freerdp_planar_switch_bgr(plain.get(), dynamic_color);
    result = CompressRow(*plain, pixels, width, compressed, size);
  }
  payload = { compressed.data(), size };
  if (result) utilities::Ensures(payload.size() <= pixels.size() + 2, "planar row fits bitmap length");
  return result != nullptr;
}
unsigned Encoder::Id(rdpSettings const* settings) const {
  utilities::Expects(settings != nullptr, "codec IDs were negotiated");
  switch (codec) {
  case SDLRDP_CODEC_REMOTEFX:
    return freerdp_settings_get_uint32(settings, FreeRDP_RemoteFxCodecId);
  case SDLRDP_CODEC_NSCODEC:
    return freerdp_settings_get_uint32(settings, FreeRDP_NSCodecId);
  case SDLRDP_CODEC_RAW:
    return RDP_CODEC_ID_NONE;
  default:
    utilities::Unreachable(codec);
  }
}
sdlrdp_codec Encoder::Codec() const noexcept {
  return codec;
}
void Encoder::Use(sdlrdp_codec value) noexcept {
  codec = value;
}
std::span<BYTE const> Encoder::Payload() const noexcept {
  return payload;
}
std::chrono::nanoseconds Encoder::EncodeTime() const noexcept {
  return encode_time;
}
void Encoder::Charge(std::chrono::nanoseconds elapsed) noexcept {
  encode_time += elapsed;
}
std::span<BYTE> Encoder::Scratch(std::size_t size) {
  scratch.resize(size);
  return scratch;
}
}
