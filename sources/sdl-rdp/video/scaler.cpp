#include <sdl-rdp/video/scaler.hpp>

#include <sdl-rdp/picture/desktop-layout.hpp>
#include <sdl-rdp/picture/frame-snapshot.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/copy-rows.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/rect.hpp>
#include <sdl-rdp/video/peer-frames.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <utility>

namespace sdl_rdp::video::detail::scaler {
using sdl_rdp::utilities::CopyRows;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::ExpectsBand;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::PixelBytes;
using sdl_rdp::utilities::RowBytes;

namespace {
auto Destination(RowOrder order, int row, int rows) -> std::size_t {
  return Narrowed<std::size_t>(order == RowOrder::BottomUp ? rows - row - 1 : row);
}
auto Blend(float left, float right, float weight) -> float {
  return left + ((right - left) * weight);
}
auto Sample(std::span<std::uint8_t const> row, Tap column, std::size_t channel) -> float {
  auto const first  = static_cast<float>(row[(column.First() * PixelBytes) + channel]);
  auto const second = static_cast<float>(row[(column.Second() * PixelBytes) + channel]);
  return Blend(first, second, column.Weight());
}
auto BlendRow(std::span<Tap const> columns, std::span<std::uint8_t const> top, std::span<std::uint8_t const> bottom,
              float weight, std::span<std::uint8_t> out) -> void {
  auto channels = std::views::iota(std::size_t{ 0 }, PixelBytes);
  for (auto [index, column] : std::views::enumerate(columns))
    for (auto channel : channels)
      // A blend of two bytes stays within 0..255, so the rounded value fits a byte.
      out[(Narrowed<std::size_t>(index) * PixelBytes) + channel] = static_cast<std::uint8_t>(
          std::floor(Blend(Sample(top, column, channel), Sample(bottom, column, channel), weight) + 0.5F));
}
auto CheckArea(sdlrdp_rect area, sdlrdp_rect desktop) -> void {
  ExpectsBand(area);
  Expects(area.x >= 0, "band left edge is nonnegative");
  Expects(area.y >= 0, "band top edge is nonnegative");
  Expects(area.x + area.w <= desktop.w, "band right edge fits the desktop");
  Expects(area.y + area.h <= desktop.h, "band bottom edge fits the desktop");
}
}
Scaler::Scaler(PeerFrames const& source, DesktopLayout const& layout) noexcept
    : _frames{ source }, _desktop{ layout } { }
auto Scaler::Areas() const -> std::vector<sdlrdp_rect> {
  return _frames.Sending() | std::views::transform([this](sdlrdp_rect rect) { return Area(rect); })
         | std::ranges::to<std::vector>();
}
auto Scaler::Target() const noexcept -> sdlrdp_rect {
  return _desktop.Rect();
}
auto Scaler::Copy(sdlrdp_rect area, std::span<std::uint8_t> buffer, RowOrder order) -> PixelBand {
  return Fill(area, buffer, RowBytes(area.w), order);
}
auto Scaler::Place(sdlrdp_rect area, std::span<std::uint8_t> buffer, std::size_t pitch) -> PixelBand {
  return Fill(area, buffer, pitch, RowOrder::TopDown);
}
auto Scaler::Area(sdlrdp_rect damage) const -> sdlrdp_rect {
  auto const& snapshot = _frames.Snapshot();
  auto const  target   = _desktop.Rect();
  Expects(snapshot.Width() > 0, "snapshot width is positive");
  Expects(snapshot.Height() > 0, "snapshot height is positive");
  if (!Scaled()) return damage;
  auto const sx     = static_cast<double>(target.w) / snapshot.Width();
  auto const sy     = static_cast<double>(target.h) / snapshot.Height();
  int const  x      = std::max(0, static_cast<int>(std::floor((damage.x - 1) * sx)));
  int const  y      = std::max(0, static_cast<int>(std::floor((damage.y - 1) * sy)));
  int const  right  = std::min(target.w, static_cast<int>(std::ceil((damage.x + damage.w + 1) * sx)));
  int const  bottom = std::min(target.h, static_cast<int>(std::ceil((damage.y + damage.h + 1) * sy)));
  return { x, y, right - x, bottom - y };
}
auto Scaler::Scaled() const -> bool {
  auto const& snapshot = _frames.Snapshot();
  return std::cmp_not_equal(_desktop.Rect().w, snapshot.Width())
         || std::cmp_not_equal(_desktop.Rect().h, snapshot.Height());
}
auto Scaler::Fill(sdlrdp_rect area, std::span<std::uint8_t> buffer, std::size_t pitch, RowOrder order) -> PixelBand {
  auto const& snapshot = _frames.Snapshot();
  auto const  stride   = RowBytes(area.w);
  auto const  size     = (Narrowed<std::size_t>(area.h - 1) * pitch) + stride;
  CheckArea(area, _desktop.Rect());
  Expects(static_cast<bool>(snapshot), "snapshot storage exists");
  Expects(pitch >= stride, "wire pitch covers the row stride");
  Expects(size <= buffer.size(), "scratch buffer covers the wire band");
  if (Scaled())
    Resample(area, buffer, pitch, order);
  else
    CopyRows(
        { .bytes = snapshot.Row(Narrowed<std::uint32_t>(area.y)).subspan(Narrowed<std::size_t>(area.x) * PixelBytes),
          .pitch = snapshot.Stride() },
        { .bytes = buffer, .pitch = pitch }, { .rows = Narrowed<std::size_t>(area.h), .row_bytes = stride },
        order == RowOrder::BottomUp);
  return { area, buffer.first(size) };
}
auto Scaler::Resample(sdlrdp_rect area, std::span<std::uint8_t> buffer, std::size_t pitch, RowOrder order) -> void {
  auto const& snapshot = _frames.Snapshot();
  auto const  ratio    = static_cast<double>(snapshot.Height()) / _desktop.Rect().h;
  Columns(area);
  for (auto row : std::views::iota(0, area.h)) {
    Tap const line{ area.y + row, ratio, snapshot.Height() };
    BlendRow(_columns, snapshot.Row(line.First()), snapshot.Row(line.Second()), line.Weight(),
             buffer.subspan(Destination(order, row, area.h) * pitch, RowBytes(area.w)));
  }
}
auto Scaler::Columns(sdlrdp_rect area) -> void {
  auto const source = _frames.Snapshot().Width();
  auto const width  = _desktop.Rect().w;
  if (_column_x == area.x && _column_width == width && _column_source == source
      && std::cmp_equal(_columns.size(), area.w))
    return;
  auto const ratio = static_cast<double>(source) / width;
  _column_x      = area.x;
  _column_width  = width;
  _column_source = source;
  _columns       = std::views::iota(0, area.w)
                   | std::views::transform([&](int x) { return Tap{ area.x + x, ratio, source }; })
                   | std::ranges::to<std::vector>();
}
}
