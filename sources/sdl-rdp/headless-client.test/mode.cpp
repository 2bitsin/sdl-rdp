#include <sdl-rdp/headless-client.test/mode.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <array>

namespace BackendGate {
auto ModeName(testing::TestParamInfo<Mode> const& info) -> std::string {
  utilities::Expects(info.param.codec >= SDLRDP_CODEC_AUTO, "codec is at least the first enumerator");
  utilities::Expects(info.param.codec <= SDLRDP_CODEC_AVC420, "codec does not exceed the final enumerator");
  constexpr std::array names{ "Auto", "Planar", "RemoteFX", "NSCodec", "Raw", "Progressive", "Avc420" };
  return std::string(names[info.param.codec]) + (info.param.surface ? "Surface" : "Bitmap");
}
auto NegotiatedCodec(sdlrdp_codec requested, bool surface) -> sdlrdp_codec {
  switch (requested) {
  case SDLRDP_CODEC_AUTO: return surface ? SDLRDP_CODEC_REMOTEFX : SDLRDP_CODEC_PLANAR;
  case SDLRDP_CODEC_REMOTEFX:
  case SDLRDP_CODEC_NSCODEC: return surface ? requested : SDLRDP_CODEC_PLANAR;
  case SDLRDP_CODEC_PLANAR:
  case SDLRDP_CODEC_RAW:
  case SDLRDP_CODEC_PROGRESSIVE:
  case SDLRDP_CODEC_AVC420: return requested;
  default:                  utilities::Unreachable(requested);
  }
}
// Lossy codecs: RemoteFX quantises, NSCodec subsamples chroma, progressive stops short of its last pass.
auto CodecTolerance(sdlrdp_codec requested, bool surface) -> std::uint32_t {
  switch (NegotiatedCodec(requested, surface)) {
  case SDLRDP_CODEC_REMOTEFX:    return 40;
  case SDLRDP_CODEC_PROGRESSIVE: return 24;
  case SDLRDP_CODEC_NSCODEC:     return 3;
  default:                       return 0;
  }
}
}
