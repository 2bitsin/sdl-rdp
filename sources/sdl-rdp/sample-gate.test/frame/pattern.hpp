#pragma once
#include <gtest/gtest.h>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <cstdint>
#include <span>

namespace sdl_rdp::sample_gate_test::frame::detail::pattern {
using sdl_rdp::headless_client_test::client::Client;

auto PatternPixel(std::span<std::uint32_t const> decoded, int index) -> std::uint32_t;
auto Pattern(Client& client, bool /*pointer*/)                       -> testing::AssertionResult;
}

namespace sdl_rdp::sample_gate_test::frame {
using detail::pattern::Pattern;
using detail::pattern::PatternPixel;
}
