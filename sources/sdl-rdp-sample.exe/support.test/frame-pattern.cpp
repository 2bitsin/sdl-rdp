#include "support.test/frame-pattern.hpp"

#include <cstddef>
#include <cstring>
#include <ranges>

namespace SampleGate {
auto PatternPixel(rdpGdi const* gdi, int index) -> UINT32 {
  UINT32 value = 0;
  std::memcpy(&value,
              gdi->primary_buffer + (static_cast<std::size_t>((index / 640)) * gdi->stride)
                  + ((static_cast<std::ptrdiff_t>(index % 640)) * 4),
              4);
  return value & 0xffffff;
}

auto Pattern(Headless::Client& client, bool /*pointer*/) -> testing::AssertionResult {
  auto* gdi = client.Instance()->context->gdi;
  if (!gdi || gdi->width != 640 || gdi->height != 480)
    return testing::AssertionFailure() << "framebuffer is not 640x480";
  auto pixel   = [&](int index) { return PatternPixel(gdi, index); };
  auto columns = std::views::iota(0, 640);
  auto first   = std::ranges::find_if(columns, [&](int x) { return pixel((40 * 640) + x) == 0x00ff00; });
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
