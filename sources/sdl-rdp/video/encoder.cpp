#include <sdl-rdp/video/encoder.hpp>

#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/extent.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/stopwatch.hpp>

#include <freerdp/constants.h>
#include <oxbox/utilities/span.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace sdl_rdp::video::detail::encoder {
using sdl_rdp::configuration::Codec;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::PixelBytes;
using sdl_rdp::utilities::Stopwatch;
using sdl_rdp::utilities::Unreachable;

auto FreeStream(wStream* stream) noexcept -> void {
  Stream_Free(stream, true);
}
namespace {
constexpr std::size_t InitialStreamCapacity = 64uz * 1024;
auto PrepareRemoteFx(RemoteFxContext& rfx) -> bool {
  if (!rfx) rfx.reset(rfx_context_new_ex(true, THREADING_FLAGS_DISABLE_THREADS));
  if (rfx) rfx_context_set_pixel_format(rfx.get(), PIXEL_FORMAT_BGRX32);
  return rfx != nullptr;
}
auto PrepareNsCodec(NsCodecContext& nsc) -> bool {
  if (!nsc) nsc.reset(nsc_context_new());
  return nsc && nsc_context_set_parameters(nsc.get(), NSC_COLOR_FORMAT, PIXEL_FORMAT_BGRX32)
         && nsc_context_set_parameters(nsc.get(), NSC_COLOR_LOSS_LEVEL, 1)
         && nsc_context_set_parameters(nsc.get(), NSC_ALLOW_SUBSAMPLING, 0);
}
auto CompressRow(BITMAP_PLANAR_CONTEXT& context, std::span<std::uint8_t const> pixels, std::uint32_t width,
                 std::span<std::byte> out, std::uint32_t& size) -> bool {
  return nullptr
         != freerdp_bitmap_compress_planar(&context, pixels.data(), PIXEL_FORMAT_BGRA32, width, 1, width * PixelBytes,
                                           oxbox::utilities::SpanCast<std::uint8_t>(out).data(), &size);
}
auto Available(rdpSettings const& settings, Codec codec) -> bool {
  auto surface = freerdp_settings_get_bool(&settings, FreeRDP_SurfaceCommandsEnabled);
  switch (codec) {
  case Codec::Planar:   return freerdp_settings_get_uint32(&settings, FreeRDP_ColorDepth) == 32;
  case Codec::RemoteFx: return surface && freerdp_settings_get_bool(&settings, FreeRDP_RemoteFxCodec);
  case Codec::NsCodec:  return surface && freerdp_settings_get_bool(&settings, FreeRDP_NSCodec);
  case Codec::Raw:      return true;
  case Codec::Avc420:
  case Codec::Progressive:
  case Codec::Auto: return false;
  default:          Unreachable(codec);
  }
}
}
auto Encoder::SetupPlanar(rdpSettings const& settings, bool xrgb) -> bool {
  auto alpha = xrgb || freerdp_settings_get_bool(&settings, FreeRDP_DrawAllowSkipAlpha);
  if (planar.skip_alpha != alpha) {
    planar.context.reset();
    planar.width = 0;
  }
  planar.skip_alpha    = alpha;
  planar.dynamic_color = freerdp_settings_get_bool(&settings, FreeRDP_DrawAllowDynamicColorFidelity);
  if (!stream) stream.reset(Stream_New(nullptr, InitialStreamCapacity));
  if (!planar.context)
    planar.context.reset(freerdp_bitmap_planar_context_new(
        PLANAR_FORMAT_HEADER_RLE | (planar.skip_alpha ? PLANAR_FORMAT_HEADER_NA : 0), 1, 1));
  return stream && planar.context;
}
auto Encoder::InitializeCodec(rdpSettings const& settings) -> bool {
  switch (codec) {
  case Codec::Planar:   return SetupPlanar(settings);
  case Codec::RemoteFx: return PrepareRemoteFx(remote_fx.context);
  case Codec::NsCodec:  return PrepareNsCodec(nsc);
  case Codec::Raw:      return true;
  default:              Unreachable(codec);
  }
}
auto Encoder::Select(rdpSettings const& settings, Codec preference) -> bool {
  if (freerdp_settings_get_uint32(&settings, FreeRDP_ColorDepth) != 32) preference = Codec::Raw;
  constexpr std::array choices{ Codec::RemoteFx, Codec::NsCodec, Codec::Planar, Codec::Raw };
  codec = Available(settings, preference)
              ? preference
              : *std::ranges::find_if(choices, [&](auto choice) { return Available(settings, choice); });
  if (!stream) stream.reset(Stream_New(nullptr, InitialStreamCapacity));
  if (!stream) return false;
  return InitializeCodec(settings);
}
auto Encoder::Encode(std::span<std::uint8_t const> pixels, std::uint32_t width, std::uint32_t height) -> bool {
  Stopwatch const watch;
  auto const      result = EncodePayload(pixels, width, height);
  Charge(watch.Elapsed());
  return result;
}
auto Encoder::ResetRemoteFx(std::uint32_t width, std::uint32_t height) -> bool {
  if (width == remote_fx.size.width && height == remote_fx.size.height) return true;
  if (!rfx_context_reset(remote_fx.context.get(), width, height)) return false;
  remote_fx.size = { .width = width, .height = height };
  return true;
}
auto Encoder::EncodeRemoteFx(std::span<std::uint8_t const> pixels, std::uint32_t width, std::uint32_t height) -> bool {
  if (!ResetRemoteFx(width, height)) return false;
  RFX_RECT const rect{ 0, 0, Narrowed<std::uint16_t>(width), Narrowed<std::uint16_t>(height) };
  return rfx_compose_message(remote_fx.context.get(), stream.get(), &rect, 1, pixels.data(), width, height, width * 4);
}
auto Encoder::EncodePayload(std::span<std::uint8_t const> pixels, std::uint32_t width, std::uint32_t height) -> bool {
  Expects(width, "encoder input is a packed band");
  Expects(height, "encoder input is a packed band");
  Expects(pixels.size() == std::size_t{ width } * height * 4, "encoder input is a packed band");
  Stream_SetPosition(stream.get(), 0);
  if (codec == Codec::Planar) {
    Expects(height == 1, "planar is row by row until sdl-rdp#42");
    return EncodePlanar(pixels, width);
  }
  bool result = false;
  if (codec == Codec::RemoteFx) {
    result = EncodeRemoteFx(pixels, width, height);
  } else if (codec == Codec::NsCodec)
    result = nsc_compose_message(nsc.get(), stream.get(), pixels.data(), width, height, width * 4);
  else
    Unreachable(codec);
  payload = oxbox::utilities::AsWritableBytes(
      std::span{ Stream_Buffer(stream.get()), Stream_GetPosition(stream.get()) });
  return result;
}
auto Encoder::EncodePlanar(std::span<std::uint8_t const> pixels, std::uint32_t width) -> bool {
  Expects(planar.context != nullptr, "planar context exists");
  Expects(pixels.size() == std::size_t{ width } * PixelBytes, "planar input is one row");
  if (width > planar.width) {
    if (!freerdp_bitmap_planar_context_reset(planar.context.get(), width, 1)) return false;
    planar.width = width;
  }
  auto& compressed = planar.compressed;
  compressed.resize(pixels.size() + 1024);
  auto size   = Narrowed<std::uint32_t>(compressed.size());
  auto result = width >= 4 && CompressRow(*planar.context, pixels, width, compressed, size);
  if (!result) {
    planar.fallback.reset(freerdp_bitmap_planar_context_new(planar.skip_alpha ? PLANAR_FORMAT_HEADER_NA : 0, width, 1));
    if (!planar.fallback) return false;
    freerdp_planar_switch_bgr(planar.fallback.get(), planar.dynamic_color);
    result = CompressRow(*planar.fallback, pixels, width, compressed, size);
  }
  payload = std::span{ compressed }.first(size);
  if (result) Ensures(payload.size() <= pixels.size() + 2, "planar row fits bitmap length");
  return result;
}
auto Encoder::Id(rdpSettings const& settings) const -> std::uint32_t {
  switch (codec) {
  case Codec::RemoteFx: return freerdp_settings_get_uint32(&settings, FreeRDP_RemoteFxCodecId);
  case Codec::NsCodec:  return freerdp_settings_get_uint32(&settings, FreeRDP_NSCodecId);
  case Codec::Raw:      return RDP_CODEC_ID_NONE;
  default:              Unreachable(codec);
  }
}
auto Encoder::SelectedCodec() const noexcept -> Codec {
  return codec;
}
auto Encoder::Use(Codec value) noexcept -> void {
  codec = value;
}
auto Encoder::Payload() const noexcept -> std::span<std::byte const> {
  return payload;
}
auto Encoder::EncodeTime() const noexcept -> std::chrono::nanoseconds {
  return encode_time;
}
auto Encoder::Charge(std::chrono::nanoseconds elapsed) noexcept -> void {
  encode_time += elapsed;
}
}
