#include <sdl-rdp/headless-client.test/codec/mode.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <array>
#include <cstdint>
#include <utility>

namespace sdl_rdp::headless_client_test::codec::detail::mode {
using sdl_rdp::configuration::Codec;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Unreachable;

auto ModeName(testing::TestParamInfo<Mode> const& info) -> std::string {
  Expects(info.param.codec >= Codec::Auto, "codec is at least the first enumerator");
  Expects(info.param.codec <= Codec::Avc420, "codec does not exceed the final enumerator");
  constexpr std::array names{ "Auto", "Planar", "RemoteFX", "NSCodec", "Raw", "Progressive", "Avc420" };
  return std::string(names[std::to_underlying(info.param.codec)]) + (info.param.surface ? "Surface" : "Bitmap");
}
auto NegotiatedCodec(Codec requested, bool surface) -> Codec {
  switch (requested) {
  case Codec::Auto: return surface ? Codec::RemoteFx : Codec::Planar;
  case Codec::RemoteFx:
  case Codec::NsCodec: return surface ? requested : Codec::Planar;
  case Codec::Planar:
  case Codec::Raw:
  case Codec::Progressive:
  case Codec::Avc420: return requested;
  default:            Unreachable(requested);
  }
}
// Lossy codecs: RemoteFX quantises, NSCodec subsamples chroma, progressive stops short of its last pass.
auto CodecTolerance(Codec requested, bool surface) -> std::uint32_t {
  switch (NegotiatedCodec(requested, surface)) {
  case Codec::RemoteFx:    return 40;
  case Codec::Progressive: return 24;
  case Codec::NsCodec:     return 3;
  default:                 return 0;
  }
}
}
