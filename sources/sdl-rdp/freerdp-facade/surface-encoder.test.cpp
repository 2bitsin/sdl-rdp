#include <sdl-rdp/freerdp-facade/surface-encoder.hpp>

#include <sdl-rdp/utilities/releases.hpp>

#include <freerdp/codec/color.h>
#include <freerdp/codec/nsc.h>
#include <freerdp/codec/region.h>
#include <freerdp/codec/rfx.h>
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

namespace sdl_rdp::freerdp_facade::detail::surface_encoder {
namespace {
using sdl_rdp::utilities::Releases;
using RemoteFxDecoder   = std::unique_ptr<RFX_CONTEXT, Releases<rfx_context_free>>;
using NscDecoder        = std::unique_ptr<NSC_CONTEXT, Releases<nsc_context_free>>;
using InitializedRegion = std::unique_ptr<REGION16, Releases<region16_uninit>>;

// A horizontal ramp, so a decoder that drops or shifts pixels shows as a mismatch.
auto Ramp(Extent size) -> std::vector<std::uint8_t> {
  auto const pixel = [&](std::size_t index) {
    return std::array<std::uint8_t, 4>{ 0x40, static_cast<std::uint8_t>(index % size.width * 4), 0xC0, 0xFF };
  };
  return std::views::iota(0uz, std::size_t{ size.width } * size.height) | std::views::transform(pixel)
         | std::views::join | std::ranges::to<std::vector>();
}
auto Near(std::span<std::uint8_t const> decoded, std::span<std::uint8_t const> expected, int tolerance) -> bool {
  return std::ranges::equal(decoded, expected, [&](int got, int want) { return std::abs(got - want) <= tolerance; });
}
auto RemoteFxDecoded(std::span<std::byte const> message, Extent size) -> std::vector<std::uint8_t> {
  RemoteFxDecoder const decoder{ rfx_context_new(false) };
  EXPECT_NE(decoder, nullptr);
  rfx_context_set_pixel_format(decoder.get(), PIXEL_FORMAT_BGRX32);
  std::vector<std::uint8_t> pixels(std::size_t{ size.width } * size.height * 4);
  REGION16                  invalid;
  region16_init(&invalid);
  InitializedRegion const owned { &invalid };
  auto const              bytes = oxbox::utilities::SpanCast<std::uint8_t const>(message);
  EXPECT_TRUE(rfx_process_message(decoder.get(), bytes.data(), static_cast<std::uint32_t>(bytes.size()), 0, 0,
                                  pixels.data(), PIXEL_FORMAT_BGRX32, size.width * 4, size.height, &invalid));
  return pixels;
}
auto NscDecoded(std::span<std::byte const> message, Extent size) -> std::vector<std::uint8_t> {
  NscDecoder const decoder{ nsc_context_new() };
  EXPECT_NE(decoder, nullptr);
  std::vector<std::uint8_t> pixels(std::size_t{ size.width } * size.height * 4);
  auto const                bytes  = oxbox::utilities::SpanCast<std::uint8_t const>(message);
  EXPECT_TRUE(nsc_process_message(decoder.get(), 32, size.width, size.height, bytes.data(),
                                  static_cast<std::uint32_t>(bytes.size()), pixels.data(), PIXEL_FORMAT_BGRX32,
                                  size.width * 4, 0, 0, size.width, size.height, FREERDP_FLIP_NONE));
  return pixels;
}
}
TEST(SurfaceEncoder, RemoteFxBandsOfEachSizeDecode) {
  SurfaceEncoder encoder{ SurfaceCodec::RemoteFx };
  for (auto const size : { Extent{ .width = 64, .height = 64 }, Extent{ .width = 128, .height = 32 } }) {
    auto const picture = Ramp(size);
    auto const message = encoder.Encode(picture, size).value_or(std::span<std::byte const>{ });
    ASSERT_FALSE(message.empty());
    EXPECT_TRUE(Near(RemoteFxDecoded(message, size), picture, 8));
  }
}
TEST(SurfaceEncoder, NsCodecBandDecodes) {
  constexpr Extent size    { .width = 64, .height = 16 };
  SurfaceEncoder   encoder { SurfaceCodec::NsCodec     };
  auto const       picture = Ramp(size);
  auto const       message = encoder.Encode(picture, size).value_or(std::span<std::byte const>{ });
  ASSERT_FALSE(message.empty());
  EXPECT_TRUE(Near(NscDecoded(message, size), picture, 2));
}
}
