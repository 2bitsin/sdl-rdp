#pragma once
#include <gtest/gtest.h>
#include <sdl-rdp/headless-client.test/client.hpp>

namespace SampleGate {
auto PatternPixel(rdpGdi const* gdi, int index)          -> UINT32;
auto Pattern(Headless::Client& client, bool /*pointer*/) -> testing::AssertionResult;
}
