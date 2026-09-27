#include <sdl-rdp/video/encoder.hpp>

#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/stopwatch.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace sdl_rdp::video::detail::encoder {
using sdl_rdp::configuration::Codec;
using sdl_rdp::freerdp_facade::BoolKey;
using sdl_rdp::freerdp_facade::NoCodecId;
using sdl_rdp::freerdp_facade::NumberKey;
using sdl_rdp::freerdp_facade::PlanarOptions;
using sdl_rdp::freerdp_facade::SurfaceCodec;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::PixelBytes;
using sdl_rdp::utilities::Stopwatch;
using sdl_rdp::utilities::Unreachable;

namespace {
// Select prepares the encoder of the codec it selects, so an unprepared one is a codec set without Select.
template <class EncoderTy> auto Prepared(std::optional<EncoderTy>& encoder, Codec codec) -> EncoderTy& {
  if (!encoder) Unreachable(codec);
  return *encoder;
}
auto Available(SettingsReader settings, Codec codec) -> bool {
  auto surface = settings.Get(BoolKey::SurfaceCommandsEnabled);
  switch (codec) {
  case Codec::Planar:   return settings.Get(NumberKey::ColorDepth) == 32;
  case Codec::RemoteFx: return surface && settings.Get(BoolKey::RemoteFxCodec);
  case Codec::NsCodec:  return surface && settings.Get(BoolKey::NSCodec);
  case Codec::Raw:      return true;
  case Codec::Avc420:
  case Codec::Progressive:
  case Codec::Auto: return false;
  default:          Unreachable(codec);
  }
}
}
auto Encoder::SetupPlanar(SettingsReader settings, bool xrgb) -> void {
  PlanarOptions const options{ .skip_alpha    = xrgb || settings.Get(BoolKey::DrawAllowSkipAlpha),
                               .dynamic_color = settings.Get(BoolKey::DrawAllowDynamicColorFidelity) };
  if (planar)
    planar->Configure(options);
  else
    planar.emplace(options);
}
auto Encoder::Prepare(SettingsReader settings) -> void {
  switch (codec) {
  case Codec::Planar: SetupPlanar(settings); return;
  case Codec::RemoteFx:
    if (!remote_fx) remote_fx.emplace(SurfaceCodec::RemoteFx);
    return;
  case Codec::NsCodec:
    if (!nsc) nsc.emplace(SurfaceCodec::NsCodec);
    return;
  case Codec::Raw: return;
  default:         Unreachable(codec);
  }
}
auto Encoder::Select(SettingsReader settings, Codec preference) -> void {
  if (settings.Get(NumberKey::ColorDepth) != 32) preference = Codec::Raw;
  constexpr std::array choices{ Codec::RemoteFx, Codec::NsCodec, Codec::Planar, Codec::Raw };
  codec = Available(settings, preference)
              ? preference
              : *std::ranges::find_if(choices, [&](auto choice) { return Available(settings, choice); });
  Prepare(settings);
}
auto Encoder::Encode(std::span<std::uint8_t const> pixels, std::uint32_t width, std::uint32_t height) -> bool {
  Expects(width, "encoder input is a packed band");
  Expects(height, "encoder input is a packed band");
  Expects(pixels.size() == std::size_t{ width } * height * PixelBytes, "encoder input is a packed band");
  Stopwatch const watch;
  auto const      encoded = Encoded(pixels, { .width = width, .height = height });
  Charge(watch.Elapsed());
  payload = encoded.value_or(std::span<std::byte const>{ });
  return encoded.has_value();
}
auto Encoder::Encoded(std::span<std::uint8_t const> pixels, Extent size) -> std::optional<std::span<std::byte const>> {
  switch (codec) {
  case Codec::Planar:
    Expects(size.height == 1, "planar is row by row until sdl-rdp#42");
    return Prepared(planar, codec).Encode(pixels);
  case Codec::RemoteFx: return Prepared(remote_fx, codec).Encode(pixels, size);
  case Codec::NsCodec:  return Prepared(nsc, codec).Encode(pixels, size);
  default:              Unreachable(codec);
  }
}
auto Encoder::Id(SettingsReader settings) const -> std::uint32_t {
  switch (codec) {
  case Codec::RemoteFx: return settings.Get(NumberKey::RemoteFxCodecId);
  case Codec::NsCodec:  return settings.Get(NumberKey::NSCodecId);
  case Codec::Raw:      return NoCodecId;
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
