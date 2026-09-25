#pragma once
#include <sdl-rdp/abi/backend.h>

#include <gtest/gtest.h>
#include <cstdint>
#include <string>

namespace BackendGate {
struct Mode {
public:
  bool         surface;
  sdlrdp_codec codec;
};
auto ModeName(testing::TestParamInfo<Mode> const& info)    -> std::string;
auto NegotiatedCodec(sdlrdp_codec requested, bool surface) -> sdlrdp_codec;
auto CodecTolerance(sdlrdp_codec requested, bool surface)  -> std::uint32_t;
}
