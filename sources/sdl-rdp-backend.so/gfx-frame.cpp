#include "_detail/activation.hpp"
#include "_detail/configuration.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/encoder.hpp"
#include "_detail/frame-pacing.hpp"
#include "_detail/gfx.hpp"
#include "_detail/peer-frames.hpp"
#include "_detail/peer-link.hpp"
#include "_detail/planar-rows.hpp"
#include "_detail/rect.hpp"
#include "_detail/scaler.hpp"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <memory>
#include <ranges>
#include <utility>
#include <winpr/sysinfo.h>

namespace Backend {
namespace {
constexpr std::size_t ProgressiveSyncBytes        = 12;
constexpr std::size_t ProgressiveContextBytes     = 10;
constexpr std::size_t ProgressiveBlockHeaderBytes = sizeof(UINT16) + sizeof(UINT32);
constexpr auto        ProgressiveHeaderBytes      = ProgressiveSyncBytes + ProgressiveContextBytes;
constexpr UINT16      ProgressiveSyncBlock        = 0xCCC0;
constexpr UINT16      ProgressiveContextBlock     = 0xCCC3;
constexpr std::size_t WireToSurfaceHeaderBytes    = 25;
RDPGFX_SURFACE_COMMAND SurfaceCommand(sdlrdp_rect area, std::span<BYTE> data, UINT32 codec) {
  RDPGFX_SURFACE_COMMAND command{ };
  command.surfaceId = GraphicsSurfaceId;
  command.codecId   = codec;
  command.contextId = GraphicsContextId;
  command.format    = PIXEL_FORMAT_BGRX32;
  command.left      = area.x;
  command.top       = area.y;
  command.right     = area.x + area.w;
  command.bottom    = area.y + area.h;
  command.width     = area.w;
  command.height    = area.h;
  command.length    = data.size();
  command.data      = data.data();
  return command;
}
bool Persistent(sdlrdp_codec codec) {
  return codec == SDLRDP_CODEC_PROGRESSIVE || codec == SDLRDP_CODEC_AVC420;
}
bool FellBack(sdlrdp_codec preference, sdlrdp_codec requested, sdlrdp_codec choice) {
  return preference == SDLRDP_CODEC_AVC420 && requested != preference && choice != preference;
}
void ExpectSurface(bool confirmed, Extent surface) {
  Expects(confirmed, "graphics capability confirmed");
  Expects(surface.width > 0, "surface width is positive");
  Expects(surface.height > 0, "surface height is positive");
}
bool EachArea(bool confirmed, Extent surface, Scaler const& scaler, std::predicate<sdlrdp_rect> auto send) {
  ExpectSurface(confirmed, surface);
  return std::ranges::all_of(scaler.Areas(), send);
}
unsigned SurfaceStride(Extent surface) {
  return Avc::Aligned(surface.width) * unsigned{ PixelBytes };
}
void ExpectInside(sdlrdp_rect area, Extent surface) {
  Expects(area.x >= 0, "command left edge is nonnegative");
  Expects(area.y >= 0, "command top edge is nonnegative");
  Expects(area.w > 0, "command width is positive");
  Expects(area.h > 0, "command height is positive");
  Expects(std::cmp_less_equal(area.x + area.w, surface.width), "command right edge fits surface");
  Expects(std::cmp_less_equal(area.y + area.h, surface.height), "command bottom edge fits surface");
}
constexpr std::array<BYTE, ProgressiveBlockHeaderBytes> BlockHeader(UINT16 block, std::size_t bytes) {
  return { BYTE(block), BYTE(block >> 8), BYTE(bytes), 0, 0, 0 };
}
// FreeRDP 3.15 rfx.c repeats SYNC/CONTEXT; GRD sends them once per surface context.
bool ProgressiveHeaders(std::span<BYTE const> data) {
  if (data.size() < ProgressiveHeaderBytes) return false;
  constexpr auto sync    = BlockHeader(ProgressiveSyncBlock, ProgressiveSyncBytes);
  constexpr auto context = BlockHeader(ProgressiveContextBlock, ProgressiveContextBytes);
  return std::ranges::equal(sync, data.first(sync.size())) &&
         std::ranges::equal(context, data.subspan(ProgressiveSyncBytes, context.size()));
}
}
sdlrdp_codec GfxChannel::CodecChoice() {
  auto choice = _configuration.Codec();
  if (choice == SDLRDP_CODEC_AUTO) choice = SDLRDP_CODEC_AVC420;
  if (choice == SDLRDP_CODEC_AVC420 && !SelectAvc()) choice = SDLRDP_CODEC_PROGRESSIVE;
  if (choice != SDLRDP_CODEC_AVC420 && choice != SDLRDP_CODEC_RAW && choice != SDLRDP_CODEC_PLANAR)
    choice = SDLRDP_CODEC_PROGRESSIVE;
  return choice;
}
std::string GfxChannel::AvcFailure() {
  if (!Avc::Encoder::Available()) return Avc::Encoder::UnavailableReason();
  if (!_avc_allowed) return "confirmed capabilities do not allow AVC420";
  if (_avc.IsOpen()) return { };
  auto const   desktop = _scaler.Target();
  Extent const size    { .width = unsigned(desktop.w), .height = unsigned(desktop.h) };
  auto const   opened  = _avc.Open(size, Avc::Bitrate(size, _configuration.AvcBitrate()), _avc_rate);
  return opened ? std::string{ } : _avc.Error();
}
bool GfxChannel::CompressProgressive(REGION16& damage, std::chrono::steady_clock::time_point start) {
  BYTE*  data    = nullptr;
  UINT32 size    = 0;
  auto   picture { Picture()               };
  auto   stride  { SurfaceStride(_surface) };
  auto   result  = progressive_compress(_progressive.get(), picture.data(), picture.size(), PIXEL_FORMAT_BGRX32,
                                        _surface.width, _surface.height, stride, &damage, &data, &size);
  _encoder.Charge(std::chrono::steady_clock::now() - start);
  return result >= 0 && data && ProgressivePayload({ data, size });
}
bool GfxChannel::ProgressiveDamage(REGION16& damage) {
  return std::ranges::all_of(_scaler.Areas(), [&](sdlrdp_rect area) {
    RECTANGLE_16 const wire{ UINT16(area.x), UINT16(area.y), UINT16(area.x + area.w), UINT16(area.y + area.h) };
    return region16_union_rect(&damage, &damage, &wire);
  });
}
std::optional<Avc::EncodingTimes> GfxChannel::AvcTimes() const {
  Expects(!_prepared.empty(), "accounting a prepared frame");
  if (_prepared.front().codec != RDPGFX_CODECID_AVC420) return std::nullopt;
  return _avc.Timing();
}
bool GfxChannel::FinishFrame() {
  _pacing.Sent(_frames, { .bytes = _frame_bytes, .encoded = _encoder.EncodeTime(), .avc = AvcTimes() });
  _last_bytes = _frame_bytes;
  _prepared.clear();
  return true;
}
bool GfxChannel::Select() {
  Expects(_confirmed, "codec follows capability confirmation");
  auto preference = _configuration.Codec();
  auto choice     = CodecChoice();
  auto previous   = _encoder.Codec();
  if (choice == SDLRDP_CODEC_PLANAR && !_encoder.SetupPlanar(&_link.Settings(), true)) return false;
  // Raw and planar do not populate the persistent progressive surface.
  if (previous != choice && Persistent(choice) && _frames.Snapshot()) _frames.Include();
  if (previous != choice) _force_idr = true;
  _encoder.Use(choice);
  bool const changed = previous != choice || FellBack(preference, _requested, choice);
  _requested = preference;
  if (changed) _activation.CodecChanged(choice);
  return true;
}
bool GfxChannel::SelectAvc() {
  Expects(_confirmed, "codec follows capability confirmation");
  auto rate = _pacing.Effective();
  if (_avc.IsOpen() && _avc_rate != rate) {
    _avc.Close();
    _force_idr = true;
  }
  _avc_rate = rate;
  bool const explicit_avc = _configuration.Codec() == SDLRDP_CODEC_AVC420;
  if (_avc_rejected && (!explicit_avc || _avc_logged)) return false;
  auto reason = AvcFailure();
  if (reason.empty()) return true;
  if (!_avc_logged && explicit_avc)
    _diagnostics.Log(SDLRDP_LOG_INFO, "AVC420 falls back to progressive: " + reason + ".");
  _avc_logged   |= explicit_avc;
  _avc_rejected =  true;
  return false;
}
std::span<BYTE const> GfxChannel::Picture() {
  auto const& snapshot = _frames.Snapshot();
  ExpectCaptured(_frames);
  if (SameSize(snapshot.Bounds(), Whole(_surface))) return snapshot.Pixels();
  auto const pitch = std::size_t(Avc::Aligned(_surface.width)) * PixelBytes;
  std::ranges::for_each(_scaler.Areas(), [&](sdlrdp_rect area) {
    auto const offset = (std::size_t(area.y) * pitch) + (std::size_t(area.x) * PixelBytes);
    _scaler.Place(area, std::span(_pixels).subspan(offset), pitch);
  });
  Avc::ReplicateEdges(_pixels, _surface);
  return _pixels;
}
bool GfxChannel::Avc420() {
  Expects(_confirmed, "graphics capability confirmed");
  Expects(_avc.IsOpen(), "AVC encoder is open");
  auto start = std::chrono::steady_clock::now();
  _regions.Clear();
  std::ranges::for_each(_scaler.Areas(), [&](sdlrdp_rect area) { _regions.Add(area); });
  auto picture{ Picture()                                          };
  auto stride { SurfaceStride(_surface)                            };
  auto data   { _avc.Encode(picture, stride, _force_idr, _payload) };
  _encoder.Charge(std::chrono::steady_clock::now() - start);
  if (data.empty()) return false;
  _force_idr   =  false;
  _frame_bytes += data.size() + WireToSurfaceHeaderBytes + _regions.Bytes();
  _prepared.push_back({ _regions.Bounds(), 0, data.size(), RDPGFX_CODECID_AVC420 });
  return true;
}
bool GfxChannel::Command(sdlrdp_rect area, std::span<BYTE const> data, UINT32 codec) {
  Expects(!data.empty(), "encoded graphics payload exists");
  _frame_bytes += data.size() + WireToSurfaceHeaderBytes;
  auto offset = _payload.size();
  _payload.insert(_payload.end(), data.begin(), data.end());
  _prepared.push_back({ area, offset, data.size(), codec });
  return true;
}
bool GfxChannel::WriteCommand(Packet const& packet) {
  Expects(_confirmed, "graphics capability confirmed");
  Expects(packet.length > 0, "graphics payload exists");
  ExpectInside(packet.area, _surface);
  auto const                  data    = std::span(_payload).subspan(packet.offset, packet.length);
  auto                        command = SurfaceCommand(packet.area, data, packet.codec);
  RDPGFX_AVC420_BITMAP_STREAM stream  { { UINT32(_regions.Rects().size()), _regions.Rects().data(),
                                          _regions.Quality().data() },
                                        UINT32(data.size()),
                                        data.data() };
  if (packet.codec == RDPGFX_CODECID_AVC420) command.extra = &stream;
  return Check(_context->SurfaceCommand(_context.get(), &command), "surface command");
}
bool GfxChannel::Progressive() {
  ExpectSurface(_confirmed, _surface);
  auto start = std::chrono::steady_clock::now();
  if (!_progressive) _progressive.reset(progressive_context_new_ex(TRUE, THREADING_FLAGS_DISABLE_THREADS));
  if (!_progressive) return false;
  REGION16 damage;
  region16_init(&damage);
  std::unique_ptr<REGION16, Releases<region16_uninit>> const owned{ &damage };
  return ProgressiveDamage(damage) && CompressProgressive(damage, start);
}
bool GfxChannel::ProgressivePayload(std::span<BYTE> data) {
  if (!ProgressiveHeaders(data)) return false;
  auto payload = data.subspan(_headers ? ProgressiveHeaderBytes : 0);
  if (!Command(Whole(_surface), payload, RDPGFX_CODECID_CAPROGRESSIVE)) return false;
  _headers = true;
  return true;
}
bool GfxChannel::Raw() {
  return EachArea(_confirmed, _surface, _scaler, [&](sdlrdp_rect area) {
    _band.resize(std::size_t(area.w) * area.h * PixelBytes);
    return Command(area, _scaler.Copy(area, _band, RowOrder::TopDown).Pixels(), RDPGFX_CODECID_UNCOMPRESSED);
  });
}
bool GfxChannel::Planar() {
  return EachArea(_confirmed, _surface, _scaler, [&](sdlrdp_rect area) {
    return EncodePlanarRows(_encoder, _scaler, area, [this](sdlrdp_rect row, std::span<BYTE const> payload) {
      return Command(row, payload, RDPGFX_CODECID_PLANAR);
    });
  });
}
bool GfxChannel::Prepare() {
  ExpectCaptured(_frames);
  Expects(_confirmed, "graphics capability confirmed");
  if (!_prepared.empty()) return true;
  return Surface() && Select();
}
void GfxChannel::BeginPayload() {
  constexpr std::size_t StartFrameBytes = 16;
  constexpr std::size_t EndFrameBytes   = 12;
  _frame_bytes = StartFrameBytes + EndFrameBytes;
  _payload.clear();
}
bool GfxChannel::Encode() {
  ExpectCaptured(_frames);
  ExpectSurface(_confirmed, _surface);
  if (!_prepared.empty()) return true;
  BeginPayload();
  auto const codec = _encoder.Codec();
  switch (codec) {
  case SDLRDP_CODEC_AVC420:
    return Avc420();
  case SDLRDP_CODEC_PROGRESSIVE:
    return Progressive();
  case SDLRDP_CODEC_PLANAR:
    return Planar();
  case SDLRDP_CODEC_RAW:
    return Raw();
  default:
    utilities::Unreachable(codec);
  }
}
bool GfxChannel::Send() {
  ExpectCaptured(_frames);
  Expects(_confirmed, "graphics capability confirmed");
  Expects(!_prepared.empty(), "frame is encoded before transport");
  if (!_pacing.Admit([this] { return FrameWindow(); })) return true;
  SYSTEMTIME time;
  GetSystemTime(&time);
  auto const                   id    = _pacing.Frame();
  RDPGFX_START_FRAME_PDU const start { FrameTimestamp(time), id };
  RDPGFX_END_FRAME_PDU const   end   { id                       };
  if (!Check(_context->StartFrame(_context.get(), &start), "start frame")) return false;
  if (!std::ranges::all_of(_prepared, [this](Packet const& packet) { return WriteCommand(packet); })) return false;
  if (!Check(_context->EndFrame(_context.get(), &end), "end frame")) return false;
  return FinishFrame();
}
}
