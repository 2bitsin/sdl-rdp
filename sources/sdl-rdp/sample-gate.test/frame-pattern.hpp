#pragma once
#include <gtest/gtest.h>
#include <sdl-rdp/headless-client.test/client.hpp>
#include <cstdint>

namespace SampleGate {
auto PatternPixel(rdpGdi const* gdi, int index)          -> std::uint32_t;
auto Pattern(Headless::Client& client, bool /*pointer*/) -> testing::AssertionResult;
}
