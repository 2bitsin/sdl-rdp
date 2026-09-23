#pragma once
#include "state.hpp"
#include "copy-rows.hpp"
#include <cmath>

namespace Backend {
inline sdlrdp_rect ScaleDamage(sdlrdp_rect r, Peer const& peer)
{
  Expects(peer.snapshot_width && peer.snapshot_height, "snapshot dimensions exist");
  auto sx = double(peer.desktop.w) / peer.snapshot_width;
  auto sy = double(peer.desktop.h) / peer.snapshot_height;
  if (sx == 1 && sy == 1) return r;
  int x = std::max(0, int(std::floor((r.x - 1) * sx)));
  int y = std::max(0, int(std::floor((r.y - 1) * sy)));
  int right = std::min(peer.desktop.w, int(std::ceil((r.x + r.w + 1) * sx)));
  int bottom = std::min(peer.desktop.h, int(std::ceil((r.y + r.h + 1) * sy)));
  return {x, y, right - x, bottom - y};
}
inline void ScaleColumns(Peer& peer, sdlrdp_rect area)
{
  if (peer.scale_x == area.x && peer.scale_width == peer.desktop.w
      && peer.scale_source == peer.snapshot_width && peer.scale_columns.size() == std::size_t(area.w)) return;
  peer.scale_x = area.x;
  peer.scale_width = peer.desktop.w;
  peer.scale_source = peer.snapshot_width;
  peer.scale_columns.resize(area.w);
  auto ratio = double(peer.snapshot_width) / peer.desktop.w;
  for (int x = 0; x < area.w; ++x) {
    auto position = std::clamp((area.x + x + 0.5) * ratio - 0.5, 0.0, double(peer.snapshot_width - 1));
    auto first = unsigned(position);
    peer.scale_columns[x] = {first * 4, std::min(first + 1, peer.snapshot_width - 1) * 4, float(position - first)};
  }
}
inline void ScaleBand(Peer& peer, sdlrdp_rect area, std::span<BYTE> buffer, bool flip, std::size_t pitch = 0)
{
  if (!pitch) pitch = std::size_t(area.w) * 4;
  Expects(peer.snapshot && area.h > 0 && pitch >= std::size_t(area.w) * 4
    && buffer.size() >= (area.h - 1) * pitch + area.w * 4, "band storage exists");
  ScaleColumns(peer, area);
  auto ratio = double(peer.snapshot_height) / peer.desktop.h;
  for (int y = 0; y < area.h; ++y) {
    auto position = std::clamp((area.y + y + 0.5) * ratio - 0.5, 0.0, double(peer.snapshot_height - 1));
    auto first = unsigned(position), second = std::min(first + 1, peer.snapshot_height - 1);
    auto weight = float(position - first);
    auto top = peer.snapshot->data() + std::size_t(first) * Avc::Aligned(peer.snapshot_width) * 4;
    auto bottom = peer.snapshot->data() + std::size_t(second) * Avc::Aligned(peer.snapshot_width) * 4;
    auto out = buffer.data() + std::size_t(flip ? area.h - y - 1 : y) * pitch;
    for (auto column : peer.scale_columns) {
      for (unsigned c = 0; c < 4; ++c) {
        float a = top[column.first + c], b = bottom[column.first + c];
        a += (top[column.second + c] - a) * column.weight;
        b += (bottom[column.second + c] - b) * column.weight;
        *out++ = BYTE(a + (b - a) * weight + 0.5f);
      }
    }
  }
}
struct Frame { sdlrdp_rect area; std::span<BYTE> pixels; };
inline auto Snapshot(Peer& peer, sdlrdp_rect area, std::span<BYTE> buffer, bool flip, std::size_t pitch = 0) -> Frame
{
  Expects(area.w > 0 && area.h > 0, "damage has positive extent");
  Expects(peer.snapshot && area.x >= 0 && area.y >= 0
    && area.x + area.w <= peer.desktop.w && area.y + area.h <= peer.desktop.h, "band fits snapshot desktop");
  auto stride = std::size_t(area.w) * 4;
  if (!pitch) pitch = stride;
  auto size = (area.h - 1) * pitch + stride;
  Expects(pitch >= stride && size <= buffer.size(), "wire band fits scratch storage");
  // Uncompressed RDP bitmap rows travel bottom-up (MS-RDPBCGR 2.2.9.1.1.3.1.2.2).
  if (peer.desktop.w != int(peer.snapshot_width) || peer.desktop.h != int(peer.snapshot_height))
    ScaleBand(peer, area, buffer, flip, pitch);
  else CopyRows(std::span<BYTE const>(*peer.snapshot).subspan(
    (std::size_t(area.y) * Avc::Aligned(peer.snapshot_width) + area.x) * 4), Avc::Aligned(peer.snapshot_width) * 4,
    buffer, pitch, area.h, stride, flip);
  return {area, buffer.first(size)};
}
}
