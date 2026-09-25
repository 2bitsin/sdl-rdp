#pragma once
#include <_buildutil/reflect.hpp>
#include <cstdint>

namespace sdl_rdp::configuration::detail::codec {
enum class Codec : std::uint8_t {
  Auto _Label("auto")               = 0,
  Planar _Label("planar")           = 1,
  RemoteFx _Label("remotefx")       = 2,
  NsCodec _Label("nscodec")         = 3,
  Raw _Label("raw")                 = 4,
  Progressive _Label("progressive") = 5,
  Avc420 _Label("avc420")           = 6
};
constexpr auto reflect_scheme(Codec* tag);
}

namespace sdl_rdp::configuration {
using detail::codec::Codec;
}
