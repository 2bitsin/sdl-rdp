#include <sdl-rdp/freerdp-facade/planar-encoder.hpp>

#include <sdl-rdp/utilities/releases.hpp>

#include <freerdp/codec/color.h>
#include <freerdp/codec/planar.h>
#include <gtest/gtest.h>
#include <oxbox/utilities/span.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ranges>
#include <span>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::planar_encoder {
namespace {
using sdl_rdp::utilities::Releases;
using Decoder = std::unique_ptr<BITMAP_PLANAR_CONTEXT, Releases<freerdp_bitmap_planar_context_free>>;

auto Row(std::uint32_t width) -> std::vector<std::uint8_t> {
  auto const pixel = [](std::uint32_t index) {
    return std::array<std::uint8_t, 4>{ static_cast<std::uint8_t>(index * 7), 0x33, static_cast<std::uint8_t>(index),
                                        0xFF };
  };
  return std::views::iota(0u, width) | std::views::transform(pixel) | std::views::join | std::ranges::to<std::vector>();
}
auto Decoded(std::span<std::byte const> bitmap, std::uint32_t width) -> std::vector<std::uint8_t> {
  Decoder const decoder{ freerdp_bitmap_planar_context_new(0, width, 1) };
  EXPECT_NE(decoder, nullptr);
  std::vector<std::uint8_t> pixels(std::size_t{ width } * 4);
  auto const                bytes  = oxbox::utilities::SpanCast<std::uint8_t const>(bitmap);
  EXPECT_TRUE(freerdp_bitmap_decompress_planar(decoder.get(), bytes.data(), static_cast<std::uint32_t>(bytes.size()),
                                               width, 1, pixels.data(), PIXEL_FORMAT_BGRA32, width * 4, 0, 0, width, 1,
                                               false));
  return pixels;
}
// The planar FormatHeader byte (MS-RDPEGDI 2.2.2.5.1) the round trip carried.
auto RoundTrip(PlanarEncoder& encoder, std::uint32_t width) -> std::uint8_t {
  auto const row    = Row(width);
  auto const bitmap = encoder.Encode(row).value_or(std::span<std::byte const>{ });
  EXPECT_FALSE(bitmap.empty());
  EXPECT_LE(bitmap.size(), row.size() + 2);
  EXPECT_EQ(Decoded(bitmap, width), row);
  return bitmap.empty() ? 0 : std::to_integer<std::uint8_t>(bitmap.front());
}
}
TEST(PlanarEncoder, EncodesRowsFreeRdpDecodes) {
  PlanarEncoder encoder{ { } };
  for (auto const width : { 64u, 200u, 32u }) RoundTrip(encoder, width);
}
TEST(PlanarEncoder, RowsNarrowerThanRleTakeTheFallback) {
  PlanarEncoder encoder{ { } };
  EXPECT_NE(RoundTrip(encoder, 64) & PLANAR_FORMAT_HEADER_RLE, 0);
  EXPECT_EQ(RoundTrip(encoder, 3) & PLANAR_FORMAT_HEADER_RLE, 0);
}
TEST(PlanarEncoder, SkippingAlphaStartsANewContext) {
  PlanarEncoder encoder{ { } };
  EXPECT_EQ(RoundTrip(encoder, 64) & PLANAR_FORMAT_HEADER_NA, 0);
  encoder.Configure({ .skip_alpha = true, .dynamic_color = false });
  EXPECT_NE(RoundTrip(encoder, 64) & PLANAR_FORMAT_HEADER_NA, 0);
}
}
