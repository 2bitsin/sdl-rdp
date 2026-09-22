#include "_detail/encoder.hpp"
#include "_detail/contract.hpp"
#include <freerdp/constants.h>
#include <array>
#include <algorithm>

namespace Backend {
namespace {
bool Available(rdpSettings const* settings, sdlrdp_codec codec)
{
  utilities::Expects(settings != nullptr, "negotiated settings exist");
  auto surface = freerdp_settings_get_bool(settings, FreeRDP_SurfaceCommandsEnabled);
  switch (codec) {
    case SDLRDP_CODEC_PLANAR: return freerdp_settings_get_uint32(settings, FreeRDP_ColorDepth) == 32;
    case SDLRDP_CODEC_REMOTEFX: return surface && freerdp_settings_get_bool(settings, FreeRDP_RemoteFxCodec);
    case SDLRDP_CODEC_NSCODEC: return surface && freerdp_settings_get_bool(settings, FreeRDP_NSCodec);
    case SDLRDP_CODEC_RAW: return true;
    case SDLRDP_CODEC_AUTO: return false;
    default: utilities::Unreachable(codec);
  }
}
}
bool Encoder::Select(rdpSettings const* settings, sdlrdp_codec preference)
{
  utilities::Expects(settings != nullptr, "negotiated settings exist");
  constexpr std::array choices{SDLRDP_CODEC_PLANAR, SDLRDP_CODEC_REMOTEFX, SDLRDP_CODEC_NSCODEC, SDLRDP_CODEC_RAW};
  codec = Available(settings, preference) ? preference
    : *std::ranges::find_if(choices, [=](auto choice) { return Available(settings, choice); });
  if (!stream) stream.reset(Stream_New(nullptr, 65536));
  if (!stream) return false;
  switch (codec) {
    case SDLRDP_CODEC_PLANAR:
      skip_alpha = freerdp_settings_get_bool(settings, FreeRDP_DrawAllowSkipAlpha);
      dynamic_color = freerdp_settings_get_bool(settings, FreeRDP_DrawAllowDynamicColorFidelity);
      if (!planar) planar.reset(freerdp_bitmap_planar_context_new(PLANAR_FORMAT_HEADER_RLE
        | (skip_alpha ? PLANAR_FORMAT_HEADER_NA : 0), 1, 1));
      return bool(planar);
    case SDLRDP_CODEC_REMOTEFX:
      if (!rfx) rfx.reset(rfx_context_new_ex(TRUE, THREADING_FLAGS_DISABLE_THREADS));
      if (rfx) rfx_context_set_pixel_format(rfx.get(), PIXEL_FORMAT_BGRX32);
      return bool(rfx);
    case SDLRDP_CODEC_NSCODEC:
      if (!nsc) nsc.reset(nsc_context_new());
      return nsc && nsc_context_set_parameters(nsc.get(), NSC_COLOR_FORMAT, PIXEL_FORMAT_BGRX32)
        && nsc_context_set_parameters(nsc.get(), NSC_COLOR_LOSS_LEVEL, 1)
        && nsc_context_set_parameters(nsc.get(), NSC_ALLOW_SUBSAMPLING, 0);
    case SDLRDP_CODEC_RAW: return true;
    default: utilities::Unreachable(codec);
  }
}
bool Encoder::Encode(std::span<BYTE const> pixels, unsigned width, unsigned height)
{
  utilities::Expects(width && height && pixels.size() == std::size_t(width) * height * 4,
                     "encoder input is a packed band");
  Stream_SetPosition(stream.get(), 0);
  if (codec == SDLRDP_CODEC_PLANAR) {
    utilities::Expects(height == 1, "planar avoids signed delta corruption in FreeRDP 3.15");
    return EncodePlanar(pixels, width);
  }
  bool result = false;
  if (codec == SDLRDP_CODEC_REMOTEFX) {
    if (width != rfx_width || height != rfx_height) {
      if (!rfx_context_reset(rfx.get(), width, height)) return false;
      rfx_width = width;
      rfx_height = height;
    }
    RFX_RECT rect{0, 0, UINT16(width), UINT16(height)};
    result = rfx_compose_message(rfx.get(), stream.get(), &rect, 1, pixels.data(), width, height, width * 4);
  } else if (codec == SDLRDP_CODEC_NSCODEC)
    result = nsc_compose_message(nsc.get(), stream.get(), pixels.data(), width, height, width * 4);
  else utilities::Unreachable(codec);
  payload = {Stream_Buffer(stream.get()), Stream_GetPosition(stream.get())};
  return result;
}
bool Encoder::EncodePlanar(std::span<BYTE const> pixels, unsigned width)
{
  utilities::Expects(planar && pixels.size() == width * 4u, "planar input is one row");
  if (width > planar_width) {
    if (!freerdp_bitmap_planar_context_reset(planar.get(), width, 1)) return false;
    planar_width = width;
  }
  compressed.resize(pixels.size() + 1024);
  UINT32 size = compressed.size();
  auto result = width < 4 ? nullptr : freerdp_bitmap_compress_planar(planar.get(), pixels.data(), PIXEL_FORMAT_BGRA32,
    width, 1, width * 4, compressed.data(), &size);
  if (!result) {
    plain.reset(freerdp_bitmap_planar_context_new(skip_alpha ? PLANAR_FORMAT_HEADER_NA : 0, width, 1));
    if (!plain) return false;
    freerdp_planar_switch_bgr(plain.get(), dynamic_color);
    result = freerdp_bitmap_compress_planar(plain.get(), pixels.data(), PIXEL_FORMAT_BGRA32,
      width, 1, width * 4, compressed.data(), &size);
  }
  payload = {compressed.data(), size};
  utilities::Ensures(!result || payload.size() <= pixels.size() + 2, "planar row fits bitmap length");
  return result != nullptr;
}
unsigned Encoder::Id(rdpSettings const* settings) const
{
  utilities::Expects(settings != nullptr, "codec IDs were negotiated");
  switch (codec) {
    case SDLRDP_CODEC_REMOTEFX: return freerdp_settings_get_uint32(settings, FreeRDP_RemoteFxCodecId);
    case SDLRDP_CODEC_NSCODEC: return freerdp_settings_get_uint32(settings, FreeRDP_NSCodecId);
    case SDLRDP_CODEC_RAW: return RDP_CODEC_ID_NONE;
    default: utilities::Unreachable(codec);
  }
}
}
