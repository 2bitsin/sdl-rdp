#pragma once
#include "copy-rows.hpp"
#include "state.hpp"

#include <cmath>
#include <cstddef>
#include <utility>

namespace Backend {
inline sdlrdp_rect ScaleDamage(sdlrdp_rect r, Peer const& peer) {
  Expects(peer.snapshot_width, "snapshot width is positive");
  Expects(peer.snapshot_height, "snapshot height is positive");
  auto sx = double(peer.desktop.w) / peer.snapshot_width;
  auto sy = double(peer.desktop.h) / peer.snapshot_height;
  if (sx == 1 && sy == 1) return r;
  int const x      = std::max(0, int(std::floor((r.x - 1) * sx)));
  int const y      = std::max(0, int(std::floor((r.y - 1) * sy)));
  int const right  = std::min(peer.desktop.w, int(std::ceil((r.x + r.w + 1) * sx)));
  int const bottom = std::min(peer.desktop.h, int(std::ceil((r.y + r.h + 1) * sy)));
  return { x, y, right - x, bottom - y };
}
inline void ScaleColumns(Peer& peer, sdlrdp_rect area) {
  if (peer.scale_x == area.x && peer.scale_width == peer.desktop.w && peer.scale_source == peer.snapshot_width &&
      peer.scale_columns.size() == std::size_t(area.w))
    return;
  peer.scale_x      = area.x;
  peer.scale_width  = peer.desktop.w;
  peer.scale_source = peer.snapshot_width;
  peer.scale_columns.resize(area.w);
  auto ratio = double(peer.snapshot_width) / peer.desktop.w;
  for (int x = 0; x < area.w; ++x) {
    auto position = std::clamp(((area.x + x + 0.5) * ratio) - 0.5, 0.0, double(peer.snapshot_width - 1));
    auto first    = unsigned(position);
    peer.scale_columns[x] = { .first = first * 4,
                              .second = std::min(first + 1, peer.snapshot_width - 1) * 4,
                              .weight = float(position - first) };
  }
}
inline void ScalePixels(auto const& columns, BYTE const* top, BYTE const* bottom, float weight, BYTE* out) {
  for (auto column : columns) {
    for (unsigned c = 0; c < 4; ++c) {
      auto a = float(top[column.first + c]);
      auto b = float(bottom[column.first + c]);
      a += (float(top[column.second + c]) - a) * column.weight;
      b += (float(bottom[column.second + c]) - b) * column.weight;
      // NOLINTNEXTLINE(bugprone-incorrect-roundings): Nonnegative wire-channel rounding.
      *out++ = BYTE(a + ((b - a) * weight) + 0.5f);
    }
  }
}
inline void ScaleBand(Peer& peer, sdlrdp_rect area, std::span<BYTE> buffer, bool flip, std::size_t pitch = 0) {
  if (!pitch) pitch = std::size_t(area.w) * 4;
  Expects(peer.snapshot != nullptr, "snapshot storage exists");
  Expects(area.h > 0, "band height is positive");
  Expects(pitch >= std::size_t(area.w) * 4, "row pitch covers the band width");
  Expects(buffer.size() >= (area.h - 1) * pitch + static_cast<std::ptrdiff_t>(area.w) * 4,
          "buffer covers the final band row");
  ScaleColumns(peer, area);
  auto ratio = double(peer.snapshot_height) / peer.desktop.h;
  for (int y = 0; y < area.h; ++y) {
    auto        position = std::clamp(((area.y + y + 0.5) * ratio) - 0.5, 0.0, double(peer.snapshot_height - 1));
    auto        first    = unsigned(position);
    auto        second   = std::min(first + 1, peer.snapshot_height - 1);
    auto        weight   = float(position - first);
    auto const* top      = peer.snapshot->data() + (std::size_t(first) * Avc::Aligned(peer.snapshot_width) * 4);
    auto const* bottom   = peer.snapshot->data() + (std::size_t(second) * Avc::Aligned(peer.snapshot_width) * 4);
    auto*       out      = buffer.data() + (std::size_t(flip ? area.h - y - 1 : y) * pitch);
    ScalePixels(peer.scale_columns, top, bottom, weight, out);
  }
}
struct Frame {
  sdlrdp_rect     area;
  std::span<BYTE> pixels;
};
inline auto Snapshot(Peer& peer, sdlrdp_rect area, std::span<BYTE> buffer, bool flip, std::size_t pitch = 0) -> Frame {
  Expects(area.w > 0, "band width is positive");
  Expects(area.h > 0, "band height is positive");
  Expects(peer.snapshot != nullptr, "snapshot storage exists");
  Expects(area.x >= 0, "band left edge is nonnegative");
  Expects(area.y >= 0, "band top edge is nonnegative");
  Expects(area.x + area.w <= peer.desktop.w, "band right edge fits the desktop");
  Expects(area.y + area.h <= peer.desktop.h, "band bottom edge fits the desktop");
  auto stride = std::size_t(area.w) * 4;
  if (!pitch) pitch = stride;
  auto size = ((area.h - 1) * pitch) + stride;
  Expects(pitch >= stride, "wire pitch covers the row stride");
  Expects(size <= buffer.size(), "scratch buffer covers the wire band");
  // Uncompressed RDP bitmap rows travel bottom-up (MS-RDPBCGR 2.2.9.1.1.3.1.2.2).
  if (std::cmp_not_equal(peer.desktop.w, peer.snapshot_width) ||
      std::cmp_not_equal(peer.desktop.h, peer.snapshot_height))
    ScaleBand(peer, area, buffer, flip, pitch);
  else
    CopyRows(std::span<BYTE const>(*peer.snapshot)
                 .subspan((std::size_t(area.y) * Avc::Aligned(peer.snapshot_width) + area.x) * 4),
             std::size_t(Avc::Aligned(peer.snapshot_width)) * 4, buffer, pitch, area.h, stride, flip);
  return { .area = area, .pixels = buffer.first(size) };
}
} // namespace Backend
