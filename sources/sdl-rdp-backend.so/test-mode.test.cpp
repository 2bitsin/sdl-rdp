#include "_detail/test-mode.hpp"

#include "_detail/contract.hpp"

#include <array>

namespace BackendGate {
auto ModeName(testing::TestParamInfo<Mode> const& info) -> std::string {
  utilities::Expects(info.param.codec >= SDLRDP_CODEC_AUTO, "codec is at least the first enumerator");
  utilities::Expects(info.param.codec <= SDLRDP_CODEC_AVC420, "codec does not exceed the final enumerator");
  constexpr std::array names{ "Auto", "Planar", "RemoteFX", "NSCodec", "Raw", "Progressive", "Avc420" };
  return std::string(names[info.param.codec]) + (info.param.surface ? "Surface" : "Bitmap");
}
}
