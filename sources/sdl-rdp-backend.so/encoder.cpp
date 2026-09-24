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
auto PrepareRemoteFx(RemoteFxContext& rfx) -> bool {
  if (!rfx) rfx.reset(rfx_context_new_ex(TRUE, THREADING_FLAGS_DISABLE_THREADS));
  if (rfx) rfx_context_set_pixel_format(rfx.get(), PIXEL_FORMAT_BGRX32);
  return bool(rfx);
}
auto PrepareNsCodec(NsCodecContext& nsc) -> bool {
  if (!nsc) nsc.reset(nsc_context_new());
  return nsc && nsc_context_set_parameters(nsc.get(), NSC_COLOR_FORMAT, PIXEL_FORMAT_BGRX32) &&
         nsc_context_set_parameters(nsc.get(), NSC_COLOR_LOSS_LEVEL, 1) &&
         nsc_context_set_parameters(nsc.get(), NSC_ALLOW_SUBSAMPLING, 0);
}
auto CompressRow(BITMAP_PLANAR_CONTEXT& context, std::span<BYTE const> pixels, unsigned width, std::span<BYTE> out,
                 UINT32& size) -> BYTE* {
  return freerdp_bitmap_compress_planar(&context, pixels.data(), PIXEL_FORMAT_BGRA32, width, 1, width * PixelBytes,
                                        out.data(), &size);
}
auto Available(rdpSettings const* settings, sdlrdp_codec codec) -> bool {
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
auto Encoder::SetupPlanar(rdpSettings const* settings, bool xrgb) -> bool {
  utilities::Expects(settings != nullptr, "negotiated settings exist");
  auto alpha = xrgb || freerdp_settings_get_bool(settings, FreeRDP_DrawAllowSkipAlpha);
  if (planar.skip_alpha != alpha) {
    planar.context.reset();
    planar.width = 0;
  }
  planar.skip_alpha    = alpha;
  planar.dynamic_color = freerdp_settings_get_bool(settings, FreeRDP_DrawAllowDynamicColorFidelity);
  if (!stream) stream.reset(Stream_New(nullptr, InitialStreamCapacity));
  if (!planar.context)
    planar.context.reset(freerdp_bitmap_planar_context_new(
        PLANAR_FORMAT_HEADER_RLE | (planar.skip_alpha ? PLANAR_FORMAT_HEADER_NA : 0), 1, 1));
  return stream && planar.context;
}
auto Encoder::InitializeCodec(rdpSettings const* settings) -> bool {
  switch (codec) {
  case SDLRDP_CODEC_PLANAR:
    return SetupPlanar(settings);
  case SDLRDP_CODEC_REMOTEFX:
    return PrepareRemoteFx(remote_fx.context);
  case SDLRDP_CODEC_NSCODEC:
    return PrepareNsCodec(nsc);
  case SDLRDP_CODEC_RAW:
    return true;
  default:
    utilities::Unreachable(codec);
  }
}
auto Encoder::Select(rdpSettings const* settings, sdlrdp_codec preference) -> bool {
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
auto Encoder::Encode(std::span<BYTE const> pixels, unsigned width, unsigned height) -> bool {
  auto start  = std::chrono::steady_clock::now();
  auto result = EncodePayload(pixels, width, height);
  Charge(std::chrono::steady_clock::now() - start);
  return result;
}
auto Encoder::ResetRemoteFx(unsigned width, unsigned height) -> bool {
  if (width == remote_fx.size.width && height == remote_fx.size.height) return true;
  if (!rfx_context_reset(remote_fx.context.get(), width, height)) return false;
  remote_fx.size = { .width = width, .height = height };
  return true;
}
auto Encoder::EncodeRemoteFx(std::span<BYTE const> pixels, unsigned width, unsigned height) -> bool {
  if (!ResetRemoteFx(width, height)) return false;
  RFX_RECT const rect{ 0, 0, UINT16(width), UINT16(height) };
  return rfx_compose_message(remote_fx.context.get(), stream.get(), &rect, 1, pixels.data(), width, height, width * 4);
}
auto Encoder::EncodePayload(std::span<BYTE const> pixels, unsigned width, unsigned height) -> bool {
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
auto Encoder::EncodePlanar(std::span<BYTE const> pixels, unsigned width) -> bool {
  utilities::Expects(planar.context != nullptr, "planar context exists");
  utilities::Expects(pixels.size() == std::size_t{ width } * PixelBytes, "planar input is one row");
  if (width > planar.width) {
    if (!freerdp_bitmap_planar_context_reset(planar.context.get(), width, 1)) return false;
    planar.width = width;
  }
  auto& compressed = planar.compressed;
  compressed.resize(pixels.size() + 1024);
  UINT32 size   = compressed.size();
  auto*  result = width < 4 ? nullptr : CompressRow(*planar.context, pixels, width, compressed, size);
  if (!result) {
    planar.fallback.reset(freerdp_bitmap_planar_context_new(planar.skip_alpha ? PLANAR_FORMAT_HEADER_NA : 0, width, 1));
    if (!planar.fallback) return false;
    freerdp_planar_switch_bgr(planar.fallback.get(), planar.dynamic_color);
    result = CompressRow(*planar.fallback, pixels, width, compressed, size);
  }
  payload = { compressed.data(), size };
  if (result) utilities::Ensures(payload.size() <= pixels.size() + 2, "planar row fits bitmap length");
  return result != nullptr;
}
auto Encoder::Id(rdpSettings const* settings) const -> unsigned {
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
auto Encoder::Codec() const noexcept -> sdlrdp_codec {
  return codec;
}
auto Encoder::Use(sdlrdp_codec value) noexcept -> void {
  codec = value;
}
auto Encoder::Payload() const noexcept -> std::span<BYTE const> {
  return payload;
}
auto Encoder::EncodeTime() const noexcept -> std::chrono::nanoseconds {
  return encode_time;
}
auto Encoder::Charge(std::chrono::nanoseconds elapsed) noexcept -> void {
  encode_time += elapsed;
}
auto Encoder::Scratch(std::size_t size) -> std::span<BYTE> {
  scratch.resize(size);
  return scratch;
}
}
