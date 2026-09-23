#include "_detail/state.hpp"
#include "_detail/scaling.hpp"
#include <array>
#include <winpr/sysinfo.h>

namespace Backend {
bool GfxChannel::Select()
{
  Expects(confirmed, "codec follows capability confirmation");
  auto preference = peer.owner.codec.load();
  auto choice = preference;
  if (choice == SDLRDP_CODEC_AUTO) choice = SDLRDP_CODEC_AVC420;
  if (choice == SDLRDP_CODEC_AVC420 && !SelectAvc()) choice = SDLRDP_CODEC_PROGRESSIVE;
  if (choice != SDLRDP_CODEC_AVC420 && choice != SDLRDP_CODEC_RAW && choice != SDLRDP_CODEC_PLANAR) choice = SDLRDP_CODEC_PROGRESSIVE;
  auto previous = peer.encoder.codec;
  if (choice == SDLRDP_CODEC_PLANAR
      && !peer.encoder.SetupPlanar(peer.client->context->settings, true)) return false;
  // Raw and planar do not populate the persistent progressive surface.
  if (previous != choice && (choice == SDLRDP_CODEC_PROGRESSIVE || choice == SDLRDP_CODEC_AVC420) && peer.snapshot)
    peer.sending.Add({0, 0, int(peer.snapshot_width), int(peer.snapshot_height)});
  if (previous != choice) force_idr = true;
  peer.encoder.codec = choice;
  bool fell_back = preference == SDLRDP_CODEC_AVC420 && requested != preference && choice != preference;
  bool changed = previous != choice || fell_back;
  requested = preference;
  if (changed && !peer.connection) peer.owner.Push({.type = SDLRDP_CODEC_CHANGED, .codec_changed = {choice}});
  return true;
}
bool GfxChannel::SelectAvc()
{
  Expects(confirmed, "codec follows capability confirmation");
  auto rate = peer.effective_refresh.load();
  if (avc.IsOpen() && avc_rate != rate) { avc.Close(); force_idr = true; }
  avc_rate = rate;
  bool explicit_avc = peer.owner.codec.load() == SDLRDP_CODEC_AVC420;
  if (avc_rejected && (!explicit_avc || avc_logged)) return false;
  std::string reason;
  if (!Avc::Encoder::Available()) reason = Avc::Encoder::UnavailableReason();
  else if (!avc_allowed) reason = "confirmed capabilities do not allow AVC420";
  else if (!avc.IsOpen()) {
    auto w = unsigned(peer.desktop.w), h = unsigned(peer.desktop.h);
    if (!avc.Open(w, h, Avc::Bitrate(w, h, peer.owner.avc_bitrate_kbps), avc_rate))
      reason = avc.Error();
  }
  if (reason.empty()) return true;
  if (!avc_logged && explicit_avc) peer.owner.Log(SDLRDP_LOG_INFO, "AVC420 falls back to progressive: " + reason + ".");
  avc_logged |= explicit_avc;
  avc_rejected = true;
  return false;
}
std::span<BYTE const> GfxChannel::Picture()
{
  Expects(peer.snapshot != nullptr, "picture snapshot exists");
  if (width == peer.snapshot_width && height == peer.snapshot_height) return *peer.snapshot;
  std::ranges::for_each(peer.sending.rects, [&](auto rect) {
    auto area = ScaleDamage(rect, peer);
    Snapshot(peer, area, std::span(pixels).subspan((std::size_t(area.y) * Avc::Aligned(width) + area.x) * 4), false, Avc::Aligned(width) * 4);
  });
  Avc::ReplicateEdges(pixels, width, height);
  return pixels;
}
bool GfxChannel::Avc420()
{
  Expects(confirmed, "graphics capability confirmed");
  Expects(avc.IsOpen(), "AVC encoder is open");
  auto start = Peer::Clock::now();
  regions.rects.clear();
  regions.quality.clear();
  std::ranges::for_each(peer.sending.rects, [&](auto rect) { regions.Add(ScaleDamage(rect, peer)); });
  auto picture { Picture()                                       };
  auto stride  { Avc::Aligned(width) * 4                         };
  auto data    { avc.Encode(picture, stride, force_idr, payload) };
  peer.encoder.encode_time += Peer::Clock::now() - start;
  if (data.empty()) return false;
  force_idr = false;
  frame_bytes += data.size() + 25 + regions.Bytes();
  prepared.push_back({regions.bounds, 0, data.size(), RDPGFX_CODECID_AVC420});
  return true;
}
void GfxChannel::AccountAvcFrame()
{
  Expects(!prepared.empty(), "accounting a prepared frame");
  if (prepared.front().codec != RDPGFX_CODECID_AVC420) return;
  ++peer.avc_frames;
  peer.avc_convert += avc.convert_time;
  peer.avc_upload += avc.upload_time;
  peer.avc_encode += avc.encode_time;
}
bool GfxChannel::Command(sdlrdp_rect area, std::span<BYTE const> data, UINT32 codec)
{
  Expects(!data.empty(), "encoded graphics payload exists");
  constexpr std::size_t WireToSurfaceHeaderBytes = 25;
  frame_bytes += data.size() + WireToSurfaceHeaderBytes;
  auto offset = payload.size();
  if (!offset) payload.assign(data.begin(), data.end());
  else payload.insert(payload.end(), data.begin(), data.end());
  prepared.push_back({area, offset, data.size(), codec});
  return true;
}
bool GfxChannel::WriteCommand(sdlrdp_rect area, std::span<BYTE> data, UINT32 codec, Avc::Regions& regions)
{
  Expects(confirmed, "graphics capability confirmed");
  Expects(!data.empty(), "graphics payload exists");
  Expects(area.x >= 0, "command left edge is nonnegative");
  Expects(area.y >= 0, "command top edge is nonnegative");
  Expects(area.w > 0, "command width is positive");
  Expects(area.h > 0, "command height is positive");
  Expects(unsigned(area.x + area.w) <= width, "command right edge fits surface");
  Expects(unsigned(area.y + area.h) <= height, "command bottom edge fits surface");
  RDPGFX_SURFACE_COMMAND command{};
  command.surfaceId = GraphicsSurfaceId;
  command.codecId = codec;
  command.contextId = GraphicsContextId;
  command.format = PIXEL_FORMAT_BGRX32;
  command.left = area.x; command.top = area.y;
  command.right = area.x + area.w; command.bottom = area.y + area.h;
  command.width = area.w; command.height = area.h;
  command.length = data.size(); command.data = data.data();
  RDPGFX_AVC420_BITMAP_STREAM stream{{UINT32(regions.rects.size()), regions.rects.data(), regions.quality.data()},
    UINT32(data.size()), data.data()};
  if (codec == RDPGFX_CODECID_AVC420) command.extra = &stream;
  return Check(context->SurfaceCommand(context.get(), &command), "surface command");
}
bool GfxChannel::Progressive()
{
  Expects(confirmed, "graphics capability confirmed");
  Expects(width > 0, "surface width is positive");
  Expects(height > 0, "surface height is positive");
  auto start = Peer::Clock::now();
  if (!progressive) progressive.reset(progressive_context_new_ex(TRUE, THREADING_FLAGS_DISABLE_THREADS));
  if (!progressive) return false;
  REGION16 damage;
  region16_init(&damage);
  for (auto rect : peer.sending.rects) {
    auto area = ScaleDamage(rect, peer);
    RECTANGLE_16 wire{UINT16(area.x), UINT16(area.y), UINT16(area.x + area.w), UINT16(area.y + area.h)};
    if (!region16_union_rect(&damage, &damage, &wire)) { region16_uninit(&damage); return false; }
  }
  BYTE* data = nullptr;
  UINT32 size = 0;
  auto picture { Picture()               };
  auto stride  { Avc::Aligned(width) * 4 };
  auto result = progressive_compress(progressive.get(), picture.data(), picture.size(), PIXEL_FORMAT_BGRX32,
    width, height, stride, &damage, &data, &size);
  peer.encoder.encode_time += Peer::Clock::now() - start;
  region16_uninit(&damage);
  return result >= 0 && data && ProgressivePayload({data, size});
}
bool GfxChannel::ProgressivePayload(std::span<BYTE> data)
{
  constexpr std::size_t ProgressiveSyncBytes = 12, ProgressiveContextBytes = 10;
  constexpr std::size_t ProgressiveBlockHeaderBytes = sizeof(UINT16) + sizeof(UINT32);
  constexpr auto ProgressiveHeaderBytes = ProgressiveSyncBytes + ProgressiveContextBytes;
  constexpr UINT16 ProgressiveSyncBlock = 0xCCC0, ProgressiveContextBlock = 0xCCC3;
  if (data.size() < ProgressiveHeaderBytes) return false;
  // FreeRDP 3.15 rfx.c repeats SYNC/CONTEXT; GRD sends them once per surface context.
  constexpr std::array<BYTE, ProgressiveBlockHeaderBytes> sync{BYTE(ProgressiveSyncBlock), BYTE(ProgressiveSyncBlock >> 8),
    ProgressiveSyncBytes, 0, 0, 0};
  constexpr std::array<BYTE, ProgressiveBlockHeaderBytes> context_header{BYTE(ProgressiveContextBlock), BYTE(ProgressiveContextBlock >> 8),
    ProgressiveContextBytes, 0, 0, 0};
  if (!std::equal(sync.begin(), sync.end(), data.begin())
      || !std::equal(context_header.begin(), context_header.end(), data.begin() + ProgressiveSyncBytes)) return false;
  auto payload = data.subspan(headers ? ProgressiveHeaderBytes : 0);
  if (!Command({0, 0, int(width), int(height)}, payload, RDPGFX_CODECID_CAPROGRESSIVE)) return false;
  headers = true;
  return true;
}
bool GfxChannel::Raw()
{
  Expects(confirmed, "graphics capability confirmed");
  Expects(width > 0, "surface width is positive");
  Expects(height > 0, "surface height is positive");
  for (auto rect : peer.sending.rects) {
    auto area = ScaleDamage(rect, peer);
    band.resize(std::size_t(area.w) * area.h * 4);
    Snapshot(peer, area, band, false);
    if (!Command(area, band, RDPGFX_CODECID_UNCOMPRESSED)) return false;
  }
  return true;
}
bool GfxChannel::Planar()
{
  Expects(confirmed, "graphics capability confirmed");
  Expects(width > 0, "surface width is positive");
  Expects(height > 0, "surface height is positive");
  auto& encoder = peer.encoder;
  for (auto rect : peer.sending.rects) {
    auto area = ScaleDamage(rect, peer);
    band.resize(std::size_t(area.w) * 4);
    for (int y = area.y; y < area.y + area.h; ++y) {
      sdlrdp_rect row{area.x, y, area.w, 1};
      Snapshot(peer, row, band, false);
      if (!encoder.Encode(band, area.w, 1)
          || !Command(row, encoder.payload, RDPGFX_CODECID_PLANAR)) return false;
    }
  }
  return true;
}
bool GfxChannel::Prepare()
{
  Expects(peer.snapshot != nullptr, "frame snapshot exists");
  Expects(confirmed, "graphics capability confirmed");
  if (!prepared.empty()) return true;
  return Surface() && Select();
}
bool GfxChannel::Encode()
{
  Expects(peer.snapshot != nullptr, "frame snapshot exists");
  Expects(confirmed, "graphics capability confirmed");
  Expects(width > 0, "surface width is positive");
  Expects(height > 0, "surface height is positive");
  if (!prepared.empty()) return true;
  constexpr std::size_t StartFrameBytes = 16, EndFrameBytes = 12;
  frame_bytes = StartFrameBytes + EndFrameBytes;
  payload.clear();
  if (peer.encoder.codec == SDLRDP_CODEC_AVC420) return Avc420();
  if (peer.encoder.codec == SDLRDP_CODEC_PROGRESSIVE) return Progressive();
  return peer.encoder.codec == SDLRDP_CODEC_PLANAR ? Planar() : Raw();
}
bool GfxChannel::Send()
{
  Expects(peer.snapshot != nullptr, "frame snapshot exists");
  Expects(confirmed, "graphics capability confirmed");
  Expects(!prepared.empty(), "frame is encoded before transport");
  { std::scoped_lock lock(peer.owner.frame_guard); if (!peer.Pacing()) return true; }
  SYSTEMTIME time;
  GetSystemTime(&time);
  RDPGFX_START_FRAME_PDU start { FrameTimestamp(time), peer.frame_id };
  RDPGFX_END_FRAME_PDU   end   { peer.frame_id                       };
  if (!Check(context->StartFrame(context.get(), &start), "start frame")) return false;
  for (auto& packet : prepared)
    if (!WriteCommand(packet.area, std::span(payload).subspan(packet.offset, packet.length), packet.codec, regions)) return false;
  if (!Check(context->EndFrame(context.get(), &end), "end frame")) return false;
  std::scoped_lock lock(peer.owner.frame_guard);
  AccountAvcFrame();
  peer.FrameSent(frame_bytes);
  last_bytes = frame_bytes;
  prepared.clear();
  peer.snapshot.reset();
  peer.sending.clear();
  return true;
}
}
