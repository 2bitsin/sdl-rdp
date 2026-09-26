#include <sdl-rdp/sample-gate.test/frame/pattern.hpp>

#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <cstddef>
#include <cstdint>
#include <ranges>

namespace sdl_rdp::sample_gate_test::frame::detail::pattern {
using sdl_rdp::headless_client_test::client::DecodedPixels;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Narrowed;

auto PatternPixel(std::span<std::uint32_t const> decoded, int index) -> std::uint32_t {
  return decoded[Narrowed<std::size_t>(index)] & 0xffffff;
}

auto Pattern(Client& client, bool /*pointer*/) -> testing::AssertionResult {
  if (client.DesktopSize() != Extent{ .width = 640, .height = 480 })
    return testing::AssertionFailure() << "framebuffer is not 640x480";
  auto const decoded = DecodedPixels(client);
  auto       pixel   = [&](int index) { return PatternPixel(decoded, index); };
  auto       columns = std::views::iota(0, 640);
  auto       first   = std::ranges::find_if(columns, [&](int x) { return pixel((40 * 640) + x) == 0x00ff00; });
  if (first == columns.end() || *first > 608) return testing::AssertionFailure() << "no complete green block on row 40";
  auto expected = [&](int index) {
    int const x        = index % 640;
    int const y        = index / 640;
    auto      expected = x >= *first && x < *first + 32 && y >= 40 && y < 72 ? 0x00ff00u : 0x010101u;
    return expected;
  };
  auto indices  = std::views::iota(0, 640 * 480);
  auto mismatch = std::ranges::find_if(indices, [&](int i) { return pixel(i) != expected(i); });
  if (mismatch == indices.end()) return testing::AssertionSuccess();
  return testing::AssertionFailure() << "pixel (" << *mismatch % 640 << "," << *mismatch / 640
                                     << ") actual=" << std::hex << pixel(*mismatch)
                                     << " expected=" << expected(*mismatch);
}
}
