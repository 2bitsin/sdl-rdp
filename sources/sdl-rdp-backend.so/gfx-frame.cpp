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
  bool explicit_avc = peer.owner.codec.load() == SDLRDP_CODEC_AVC420;
  if (avc_rejected && (!explicit_avc || avc_logged)) return false;
  std::string reason;
  if (!Avc::Encoder::Available()) reason = Avc::Encoder::UnavailableReason();
  else if (!avc_allowed) reason = "confirmed capabilities do not allow AVC420";
  else if (!avc.IsOpen()) {
    auto w = unsigned(peer.desktop.w), h = unsigned(peer.desktop.h);
    if (!avc.Open(w, h, Avc::Bitrate(w, h, peer.owner.avc_bitrate_kbps), std::max(1u, peer.refresh ? peer.refresh / 1000 : 60)))
      reason = avc.Error();
  }
  if (reason.empty()) return true;
  if (!avc_logged && explicit_avc) peer.owner.Log(SDLRDP_LOG_INFO, "AVC420 falls back to progressive: " + reason + ".");
  avc_logged |= explicit_avc;
  avc_rejected = true;
  return false;
}
bool GfxChannel::Avc420()
{
  Expects(confirmed && width && height && avc.IsOpen(), "confirmed AVC surface exists");
  auto start = Peer::Clock::now();
  Avc::Regions regions;
  for (auto rect : peer.sending.rects) {
    auto area = ScaleDamage(rect, peer);
    Snapshot(peer, area, std::span(pixels).subspan((std::size_t(area.y) * width + area.x) * 4), false, width * 4);
    regions.Add(area);
  }
  auto data = avc.Encode(pixels, width * 4, force_idr);
  peer.encoder.encode_time += Peer::Clock::now() - start;
  if (data.empty()) return false;
  force_idr = false;
  auto bounds = regions.bounds;
  return Command(bounds, data, RDPGFX_CODECID_AVC420, std::move(regions));
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
bool GfxChannel::Command(sdlrdp_rect area, std::span<BYTE const> data, UINT32 codec, Avc::Regions regions)
{
  Expects(!data.empty(), "encoded graphics payload exists");
  constexpr std::size_t WireToSurfaceHeaderBytes = 25;
  frame_bytes += data.size() + WireToSurfaceHeaderBytes;
  if (codec == RDPGFX_CODECID_AVC420) frame_bytes += regions.Bytes();
  prepared.push_back({area, {data.begin(), data.end()}, codec, std::move(regions)});
  return true;
}
bool GfxChannel::WriteCommand(sdlrdp_rect area, std::span<BYTE> data, UINT32 codec, Avc::Regions& regions)
{
  Expects(confirmed && !data.empty(), "confirmed graphics payload exists");
  Expects(area.x >= 0 && area.y >= 0 && area.w > 0 && area.h > 0
    && unsigned(area.x + area.w) <= width && unsigned(area.y + area.h) <= height,
    "command rectangle fits the surface");
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
  Expects(confirmed && width && height, "confirmed surface exists");
  auto start = Peer::Clock::now();
  if (!progressive) progressive.reset(progressive_context_new_ex(TRUE, THREADING_FLAGS_DISABLE_THREADS));
  if (!progressive) return false;
  REGION16 damage;
  region16_init(&damage);
  for (auto rect : peer.sending.rects) {
    auto area = ScaleDamage(rect, peer);
    Snapshot(peer, area, std::span(pixels).subspan((std::size_t(area.y) * width + area.x) * 4), false, width * 4);
    RECTANGLE_16 wire{UINT16(area.x), UINT16(area.y), UINT16(area.x + area.w), UINT16(area.y + area.h)};
    if (!region16_union_rect(&damage, &damage, &wire)) { region16_uninit(&damage); return false; }
  }
  BYTE* data = nullptr;
  UINT32 size = 0;
  auto result = progressive_compress(progressive.get(), pixels.data(), pixels.size(), PIXEL_FORMAT_BGRX32,
    width, height, width * 4, &damage, &data, &size);
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
  Expects(confirmed && width && height, "confirmed surface exists");
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
  Expects(confirmed && width && height, "confirmed surface exists");
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
  Expects(peer.snapshot && confirmed, "confirmed frame snapshot exists");
  if (!prepared.empty()) return true;
  return Surface() && Select();
}
bool GfxChannel::Encode()
{
  Expects(peer.snapshot && confirmed && width && height, "confirmed frame surface exists");
  if (!prepared.empty()) return true;
  constexpr std::size_t StartFrameBytes = 16, EndFrameBytes = 12;
  frame_bytes = StartFrameBytes + EndFrameBytes;
  if (peer.encoder.codec == SDLRDP_CODEC_AVC420) return Avc420();
  if (peer.encoder.codec == SDLRDP_CODEC_PROGRESSIVE) return Progressive();
  return peer.encoder.codec == SDLRDP_CODEC_PLANAR ? Planar() : Raw();
}
bool GfxChannel::Send()
{
  Expects(peer.snapshot && confirmed, "confirmed frame snapshot exists");
  Expects(!prepared.empty(), "frame is encoded before transport");
  { std::scoped_lock lock(peer.owner.frame_guard); if (!peer.Pacing()) return true; }
  SYSTEMTIME time;
  GetSystemTime(&time);
  RDPGFX_START_FRAME_PDU start{FrameTimestamp(time), peer.frame_id};
  RDPGFX_END_FRAME_PDU end{peer.frame_id};
  if (!Check(context->StartFrame(context.get(), &start), "start frame")) return false;
  for (auto& packet : prepared)
    if (!WriteCommand(packet.area, packet.data, packet.codec, packet.regions)) return false;
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
