#include <sdl-rdp/freerdp-facade/progressive-encoder.hpp>

#include <sdl-rdp/utilities/releases.hpp>

#include <freerdp/codec/color.h>
#include <freerdp/codec/progressive.h>
#include <freerdp/codec/region.h>
#include <gtest/gtest.h>
#include <oxbox/utilities/span.hpp>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <ranges>
#include <span>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::progressive_encoder {
namespace {
using sdl_rdp::utilities::Releases;
using Decoder           = std::unique_ptr<PROGRESSIVE_CONTEXT, Releases<progressive_context_free>>;
using InitializedRegion = std::unique_ptr<REGION16, Releases<region16_uninit>>;
constexpr Extent                      Surface   { .width = 128, .height = 64 };
constexpr std::uint32_t               Stride    = Surface.width * 4;
constexpr std::uint16_t               SurfaceId = 0;
constexpr std::array<std::uint8_t, 4> Amber     { 0x10, 0xA0, 0xF0, 0xFF     };

auto Solid() -> std::vector<std::uint8_t> {
  return std::views::repeat(Amber, std::size_t{ Surface.width } * Surface.height) | std::views::join
         | std::ranges::to<std::vector>();
}
auto Decoded(std::span<std::byte const> message) -> std::vector<std::uint8_t> {
  Decoder const decoder{ progressive_context_new(false) };
  EXPECT_NE(decoder, nullptr);
  EXPECT_GE(progressive_create_surface_context(decoder.get(), SurfaceId, Surface.width, Surface.height), 0);
  std::vector<std::uint8_t> pixels(std::size_t{ Stride } * Surface.height);
  REGION16                  invalid;
  region16_init(&invalid);
  InitializedRegion const owned { &invalid };
  auto const              bytes = oxbox::utilities::SpanCast<std::uint8_t const>(message);
  EXPECT_GE(progressive_decompress(decoder.get(), bytes.data(), static_cast<std::uint32_t>(bytes.size()), pixels.data(),
                                   PIXEL_FORMAT_BGRX32, Stride, 0, 0, &invalid, SurfaceId, 0),
            0);
  return pixels;
}
}
TEST(ProgressiveEncoder, CompressesTheUnionOfItsDamage) {
  ProgressiveEncoder   encoder;
  auto const           picture = Solid();
  constexpr std::array damage  { Rect{ .x = 0, .y = 0, .w = 64, .h = 64 }, Rect{ .x = 64, .y = 0, .w = 64, .h = 64 } };
  auto const message = encoder.Compress(picture, Stride, Surface, damage).value_or(std::span<std::byte const>{ });
  ASSERT_FALSE(message.empty());
  auto const decoded = Decoded(message);
  EXPECT_TRUE(std::ranges::equal(decoded, picture, [](int got, int want) { return std::abs(got - want) <= 4; }));
}
}
