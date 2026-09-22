#include "_detail/state.hpp"
#include <freerdp/constants.h>
#include <freerdp/settings.h>
#include <freerdp/update.h>
#include <algorithm>
#include <span>
#include <array>
#include "_detail/copy-rows.hpp"
namespace Backend {
namespace {
constexpr std::size_t BITMAP_RECTANGLE_LIMIT = 0xFFFF;
struct Frame { sdlrdp_rect area; std::span<BYTE> pixels; };

auto SendSurfaceBits(rdpUpdate* update, Frame const& what) -> bool
{
  Expects(update && update->SurfaceBits, "surface callback exists");
  auto payload = what.pixels;
  auto command = SURFACE_BITS_COMMAND{ };
  command.cmdType = CMDTYPE_SET_SURFACE_BITS;
  command.skipCompression = TRUE;
  command.destLeft = what.area.x;
  command.destTop = what.area.y;
  command.destRight = (what.area.x + what.area.w);
  command.destBottom = (what.area.y + what.area.h);
  command.bmp.bpp = 32u;
  command.bmp.codecID = RDP_CODEC_ID_NONE;
  command.bmp.width = static_cast<UINT16>(what.area.w);
  command.bmp.height = static_cast<UINT16>(what.area.h);
  command.bmp.bitmapDataLength = static_cast<UINT32>(payload.size());
  command.bmp.bitmapData = payload.data();
  return update->SurfaceBits(update->context, &command);
}

auto SendBitmapBand(rdpUpdate* update, Frame const& band) -> bool
{
  Expects(update && update->BitmapUpdate, "bitmap callback exists");
  auto payload = band.pixels;
  auto rectangle = BITMAP_DATA{ };
  rectangle.destLeft = band.area.x;
  rectangle.destTop = band.area.y;
  // Bitmap update corners are inclusive, unlike a surface command's.
  rectangle.destRight = (band.area.x + band.area.w) - 1u;
  rectangle.destBottom = (band.area.y + band.area.h) - 1u;
  rectangle.width = band.area.w;
  rectangle.height = band.area.h;
  rectangle.bitsPerPixel = 32u;
  rectangle.bitmapLength = static_cast<UINT32>(payload.size());
  rectangle.bitmapDataStream = payload.data();
  rectangle.compressed = FALSE;
  auto batch = BITMAP_UPDATE{ };
  batch.number = 1u;
  batch.rectangles = &rectangle;
  batch.skipCompression = TRUE;
  return update->BitmapUpdate(update->context, &batch);
}

auto Snapshot(State& state, sdlrdp_rect area, std::span<BYTE> buffer) -> Frame
{
  Expects(area.w > 0 && area.h > 0, "damage has positive extent");
  std::scoped_lock lock(state.frame_guard);
  area.w = std::min(area.w, int(state.frame_width) - area.x);
  area.h = std::min(area.h, int(state.frame_height) - area.y);
  if (area.w <= 0 || area.h <= 0) return {{}, {}};
  auto stride = std::size_t(area.w) * 4;
  Expects(stride * area.h <= buffer.size(), "wire band fits scratch storage");
  // Uncompressed RDP bitmap rows travel bottom-up (MS-RDPBCGR 2.2.9.1.1.3.1.2.2).
  CopyRows(std::span<BYTE const>(state.shadow).subspan(
    (std::size_t(area.y) * state.frame_width + area.x) * 4), state.frame_width * 4,
    buffer, stride, area.h, stride);
  auto rows = buffer.first(stride * area.h) | std::views::chunk(stride);
  for (auto index : std::views::iota(0, area.h / 2))
    std::ranges::swap_ranges(rows[index], (rows | std::views::reverse)[index]);
  return {area, buffer.first(stride * area.h)};
}
}
bool SendFrame(rdpContext* context, State& state, sdlrdp_rect area)
{
  Expects(context && area.w > 0 && area.h > 0, "frame destination and damage exist");
  std::array<BYTE, BITMAP_RECTANGLE_LIMIT> buffer;
  auto lines = int(buffer.size() / (std::size_t(area.w) * 4));
  Expects(lines > 0, "one row fits the wire band");
  auto surface = freerdp_settings_get_bool(context->settings, FreeRDP_SurfaceCommandsEnabled);
  for (int first = 0; first < area.h; first += lines) {
    if (context->peer->IsWriteBlocked(context->peer)) {
      std::scoped_lock lock(state.frame_guard);
      Peer::Held(context->peer).Post({area.x, area.y + first, area.w, area.h - first});
      return true;
    }
    auto band = Snapshot(state, {area.x, area.y + first, area.w,
                                std::min(lines, area.h - first)}, buffer);
    if (band.pixels.empty()) continue;
    if (!(surface ? SendSurfaceBits(context->update, band)
                  : SendBitmapBand(context->update, band))) return false;
  }
  return true;
}
}
