#pragma once
#include <sdl-rdp/configuration/codec.hpp>

#include <gtest/gtest.h>
#include <cstdint>
#include <string>

namespace sdl_rdp::headless_client_test::codec::detail::mode {
using sdl_rdp::configuration::Codec;
struct Mode {
public:
  bool  surface;
  Codec codec;
};
auto ModeName(testing::TestParamInfo<Mode> const& info) -> std::string;
auto NegotiatedCodec(Codec requested, bool surface)     -> Codec;
auto CodecTolerance(Codec requested, bool surface)      -> std::uint32_t;
}

namespace sdl_rdp::headless_client_test::codec {
using detail::mode::CodecTolerance;
using detail::mode::Mode;
using detail::mode::ModeName;
using detail::mode::NegotiatedCodec;
}
