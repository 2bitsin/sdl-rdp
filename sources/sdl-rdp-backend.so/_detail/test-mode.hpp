#pragma once
#include "sdl-rdp-backend.h"

#include <gtest/gtest.h>
#include <string>

namespace BackendGate {
struct Mode {
public:
  bool         surface;
  sdlrdp_codec codec;
};
std::string ModeName(testing::TestParamInfo<Mode> const& info);
}
