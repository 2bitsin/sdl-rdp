#include <sdl-rdp/freerdp-facade/planar-encoder.hpp>

#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <freerdp/codec/color.h>
#include <freerdp/codec/planar.h>
#include <oxbox/utilities/span.hpp>
#include <vector>

namespace sdl_rdp::freerdp_facade::detail::planar_encoder {
using sdl_rdp::utilities::AllocationFailed;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::PixelBytes;
using sdl_rdp::utilities::Releases;
using sdl_rdp::utilities::Stride;

namespace {
using PlanarContext = std::unique_ptr<BITMAP_PLANAR_CONTEXT, Releases<freerdp_bitmap_planar_context_free>>;
constexpr std::size_t   CompressionSlack = 1024;
constexpr std::uint32_t RleMinimumWidth  = 4;
auto AlphaFlag(PlanarOptions options) -> std::uint32_t {
  return options.skip_alpha ? PLANAR_FORMAT_HEADER_NA : 0;
}
auto NewContext(PlanarOptions options) -> PlanarContext {
  PlanarContext context{ freerdp_bitmap_planar_context_new(PLANAR_FORMAT_HEADER_RLE | AlphaFlag(options), 1, 1) };
  if (!context) throw AllocationFailed{ "Planar context" };
  return context;
}
auto Width(std::span<std::uint8_t const> row) -> std::uint32_t {
  return Narrowed<std::uint32_t>(row.size() / PixelBytes);
}
auto CompressRow(BITMAP_PLANAR_CONTEXT& context, std::span<std::uint8_t const> row, std::span<std::byte> out,
                 std::uint32_t& size) -> bool {
  auto const width = Width(row);
  return nullptr
         != freerdp_bitmap_compress_planar(&context, row.data(), PIXEL_FORMAT_BGRA32, width, 1, Stride(width),
                                           oxbox::utilities::SpanCast<std::uint8_t>(out).data(), &size);
}
}
struct PlanarEncoder::State {
  PlanarOptions          options;
  PlanarContext          context   { NewContext(options) };
  std::vector<std::byte> compressed;
  std::uint32_t          width     { };
};
PlanarEncoder::PlanarEncoder(PlanarOptions options) : _state{ std::make_unique<State>(options) } { }
PlanarEncoder::~PlanarEncoder() = default;
auto PlanarEncoder::Configure(PlanarOptions options) -> void {
  if (options.skip_alpha != _state->options.skip_alpha) {
    _state->context = NewContext(options);
    _state->width   = 0;
  }
  _state->options = options;
}
auto PlanarEncoder::Grow(std::uint32_t width) -> bool {
  if (width <= _state->width) return true;
  if (!freerdp_bitmap_planar_context_reset(_state->context.get(), width, 1)) return false;
  _state->width = width;
  return true;
}
auto PlanarEncoder::Fallback(std::span<std::uint8_t const> row, std::uint32_t& size) -> bool {
  PlanarContext const fallback{ freerdp_bitmap_planar_context_new(AlphaFlag(_state->options), Width(row), 1) };
  if (!fallback) return false;
  freerdp_planar_switch_bgr(fallback.get(), _state->options.dynamic_color);
  return CompressRow(*fallback, row, _state->compressed, size);
}
auto PlanarEncoder::Encode(std::span<std::uint8_t const> row) -> std::optional<std::span<std::byte const>> {
  Expects(!row.empty(), "a planar row has pixels");
  Expects(row.size() % PixelBytes == 0, "a planar row is whole pixels");
  auto const width = Width(row);
  if (!Grow(width)) return std::nullopt;
  _state->compressed.resize(row.size() + CompressionSlack);
  auto       size       = Narrowed<std::uint32_t>(_state->compressed.size());
  auto const compressed = width >= RleMinimumWidth && CompressRow(*_state->context, row, _state->compressed, size);
  if (!compressed && !Fallback(row, size)) return std::nullopt;
  auto const bitmap = std::span<std::byte const>{ _state->compressed }.first(size);
  Ensures(bitmap.size() <= row.size() + 2, "planar row fits bitmap length");
  return bitmap;
}
}
