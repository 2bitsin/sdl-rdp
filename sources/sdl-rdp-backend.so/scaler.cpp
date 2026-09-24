#include "_detail/scaler.hpp"

#include "_detail/contract.hpp"
#include "_detail/copy-rows.hpp"
#include "_detail/desktop-layout.hpp"
#include "_detail/frame-snapshot.hpp"
#include "_detail/peer-frames.hpp"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <utility>

namespace Backend {
namespace {
auto Destination(RowOrder order, int row, int rows) -> std::size_t {
  return std::size_t(order == RowOrder::BottomUp ? rows - row - 1 : row);
}
auto Blend(float left, float right, float weight) -> float {
  return left + ((right - left) * weight);
}
auto Sample(std::span<BYTE const> row, Tap column, std::size_t channel) -> float {
  auto const first  = float(row[(column.First() * PixelBytes) + channel]);
  auto const second = float(row[(column.Second() * PixelBytes) + channel]);
  return Blend(first, second, column.Weight());
}
auto BlendRow(std::span<Tap const> columns, std::span<BYTE const> top, std::span<BYTE const> bottom, float weight,
              std::span<BYTE> out) -> void {
  auto channels = std::views::iota(std::size_t{ 0 }, PixelBytes);
  for (auto [index, column] : std::views::enumerate(columns))
    for (auto channel : channels)
      out[(std::size_t(index) * PixelBytes) + channel] =
          BYTE(std::floor(Blend(Sample(top, column, channel), Sample(bottom, column, channel), weight) + 0.5F));
}
auto CheckArea(sdlrdp_rect area, sdlrdp_rect desktop) -> void {
  Expects(area.w > 0, "band width is positive");
  Expects(area.h > 0, "band height is positive");
  Expects(area.x >= 0, "band left edge is nonnegative");
  Expects(area.y >= 0, "band top edge is nonnegative");
  Expects(area.x + area.w <= desktop.w, "band right edge fits the desktop");
  Expects(area.y + area.h <= desktop.h, "band bottom edge fits the desktop");
}
}
Scaler::Scaler(PeerFrames const& source, DesktopLayout const& layout) noexcept
    : _frames { source }, _desktop{ layout } { }
auto Scaler::Areas() const -> std::vector<sdlrdp_rect> {
  return _frames.Sending() | std::views::transform([this](sdlrdp_rect rect) { return Area(rect); }) |
         std::ranges::to<std::vector>();
}
auto Scaler::Target() const noexcept -> sdlrdp_rect {
  return _desktop.Rect();
}
auto Scaler::Copy(sdlrdp_rect area, std::span<BYTE> buffer, RowOrder order) -> PixelBand {
  return Fill(area, buffer, std::size_t(area.w) * PixelBytes, order);
}
auto Scaler::Place(sdlrdp_rect area, std::span<BYTE> buffer, std::size_t pitch) -> PixelBand {
  return Fill(area, buffer, pitch, RowOrder::TopDown);
}
auto Scaler::Area(sdlrdp_rect damage) const -> sdlrdp_rect {
  auto const& snapshot = _frames.Snapshot();
  auto const  target   = _desktop.Rect();
  Expects(snapshot.Width() > 0, "snapshot width is positive");
  Expects(snapshot.Height() > 0, "snapshot height is positive");
  if (!Scaled()) return damage;
  auto const sx     = double(target.w) / snapshot.Width();
  auto const sy     = double(target.h) / snapshot.Height();
  int const  x      = std::max(0, int(std::floor((damage.x - 1) * sx)));
  int const  y      = std::max(0, int(std::floor((damage.y - 1) * sy)));
  int const  right  = std::min(target.w, int(std::ceil((damage.x + damage.w + 1) * sx)));
  int const  bottom = std::min(target.h, int(std::ceil((damage.y + damage.h + 1) * sy)));
  return { x, y, right - x, bottom - y };
}
auto Scaler::Scaled() const -> bool {
  auto const& snapshot = _frames.Snapshot();
  return std::cmp_not_equal(_desktop.Rect().w, snapshot.Width()) ||
         std::cmp_not_equal(_desktop.Rect().h, snapshot.Height());
}
auto Scaler::Fill(sdlrdp_rect area, std::span<BYTE> buffer, std::size_t pitch, RowOrder order) -> PixelBand {
  auto const& snapshot = _frames.Snapshot();
  auto const  stride   = std::size_t(area.w) * PixelBytes;
  auto const  size     = (std::size_t(area.h - 1) * pitch) + stride;
  CheckArea(area, _desktop.Rect());
  Expects(bool(snapshot), "snapshot storage exists");
  Expects(pitch >= stride, "wire pitch covers the row stride");
  Expects(size <= buffer.size(), "scratch buffer covers the wire band");
  if (Scaled())
    Resample(area, buffer, pitch, order);
  else
    CopyRows({ .bytes = snapshot.Row(unsigned(area.y)).subspan(std::size_t(area.x) * PixelBytes),
               .pitch = snapshot.Stride() },
             { .bytes = buffer, .pitch = pitch }, { .rows = std::size_t(area.h), .row_bytes = stride },
             order == RowOrder::BottomUp);
  return { area, buffer.first(size) };
}
auto Scaler::Resample(sdlrdp_rect area, std::span<BYTE> buffer, std::size_t pitch, RowOrder order) -> void {
  auto const& snapshot = _frames.Snapshot();
  auto const  ratio    = double(snapshot.Height()) / _desktop.Rect().h;
  Columns(area);
  for (auto row : std::views::iota(0, area.h)) {
    Tap const line{ area.y + row, ratio, snapshot.Height() };
    BlendRow(_columns, snapshot.Row(line.First()), snapshot.Row(line.Second()), line.Weight(),
             buffer.subspan(Destination(order, row, area.h) * pitch, std::size_t(area.w) * PixelBytes));
  }
}
auto Scaler::Columns(sdlrdp_rect area) -> void {
  auto const source = _frames.Snapshot().Width();
  auto const width  = _desktop.Rect().w;
  if (_column_x == area.x && _column_width == width && _column_source == source &&
      _columns.size() == std::size_t(area.w))
    return;
  auto const ratio = double(source) / width;
  _column_x      = area.x;
  _column_width  = width;
  _column_source = source;
  _columns = std::views::iota(0, area.w) |
            std::views::transform([&](int x) { return Tap{ area.x + x, ratio, source }; }) |
            std::ranges::to<std::vector>();
}
}
