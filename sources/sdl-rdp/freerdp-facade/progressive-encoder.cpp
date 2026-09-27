#include <sdl-rdp/freerdp-facade/progressive-encoder.hpp>

#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <freerdp/codec/color.h>
#include <freerdp/codec/progressive.h>
#include <freerdp/codec/region.h>
#include <freerdp/settings_types.h>
#include <oxbox/utilities/span.hpp>
#include <algorithm>

namespace sdl_rdp::freerdp_facade::detail::progressive_encoder {
using sdl_rdp::utilities::AllocationFailed;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Stride;
using sdl_rdp::utilities::Releases;

namespace {
using ProgressiveContext = std::unique_ptr<PROGRESSIVE_CONTEXT, Releases<progressive_context_free>>;
using InitializedRegion  = std::unique_ptr<REGION16, Releases<region16_uninit>>;
auto NewContext() -> ProgressiveContext {
  ProgressiveContext context{ progressive_context_new_ex(true, THREADING_FLAGS_DISABLE_THREADS) };
  if (!context) throw AllocationFailed{ "Progressive context" };
  return context;
}
auto Wire(Rect area) -> RECTANGLE_16 {
  return { Narrowed<std::uint16_t>(area.x), Narrowed<std::uint16_t>(area.y), Narrowed<std::uint16_t>(area.x + area.w),
           Narrowed<std::uint16_t>(area.y + area.h) };
}
auto Unite(REGION16& region, std::span<Rect const> areas) -> bool {
  return std::ranges::all_of(areas, [&](Rect area) {
    auto const wire = Wire(area);
    return region16_union_rect(&region, &region, &wire);
  });
}
}
struct ProgressiveEncoder::State {
  ProgressiveContext context{ NewContext() };
};
ProgressiveEncoder::ProgressiveEncoder() : _state{ std::make_unique<State>() } { }
ProgressiveEncoder::~ProgressiveEncoder() = default;
auto ProgressiveEncoder::Compress(std::span<std::uint8_t const> bgrx, std::uint32_t stride, Extent size,
                                  std::span<Rect const> damage) -> std::optional<std::span<std::byte const>> {
  Expects(size.width > 0, "progressive surface width is positive");
  Expects(size.height > 0, "progressive surface height is positive");
  auto const row = Stride(size.width);
  Expects(stride >= row, "progressive stride covers the surface width");
  Expects(bgrx.size() >= (std::size_t{ stride } * (size.height - 1)) + row, "progressive picture covers the surface");
  REGION16 region;
  region16_init(&region);
  InitializedRegion const owned{ &region };
  if (!Unite(region, damage)) return std::nullopt;
  std::uint8_t* data   = nullptr;
  std::uint32_t length = 0;
  auto const    result = progressive_compress(_state->context.get(), bgrx.data(), Narrowed<std::uint32_t>(bgrx.size()),
                                              PIXEL_FORMAT_BGRX32, size.width, size.height, stride, &region, &data,
                                              &length);
  if (result < 0 || data == nullptr) return std::nullopt;
  // abi: the context lends its message as a pointer and a length.
  return oxbox::utilities::AsBytes(std::span{ data, length });
}
}
