#include "_detail/avc.hpp"

#include <array>
#include <cstddef>
#include <winpr/wlog.h>
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): Required by the ffnvcodec loader.
#define FFNV_LOG_FUNC(ctx, msg, ...) WLog_ERR("sdlrdp.avc", msg, __VA_ARGS__)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): Required by the ffnvcodec loader.
#define FFNV_DEBUG_LOG_FUNC(ctx, msg, ...) ((void)0)
// WinPR already supplies the ABI-compatible GUID type.
#define GUID_DEFINED
#include <ffnvcodec/dynlink_loader.h>
#include <format>
#include <freerdp/primitives.h>
#include <oxbox/utilities/bits.hpp>
#include <ranges>
#include <utility>

namespace Backend::Avc {
using utilities::Ensures;
using utilities::Expects;
IntraRefresh IntraRefreshFor(unsigned fps) {
  Expects(fps, "refresh rate is positive");
  Expects(fps <= UINT32_MAX / 2, "doubled refresh rate fits NVENC");
  // Recovery target: refresh every two seconds, spreading each sweep over half a second.
  IntraRefresh refresh{ .period = 2 * fps, .count = std::max(1u, fps / 2) };
  Ensures(refresh.count <= refresh.period, "refresh sweep fits its period");
  return refresh;
}
unsigned Aligned(unsigned dimension) {
  Expects(dimension > 0, "surface dimension is positive");
  Expects(dimension <= 32766, "surface dimension fits the graphics protocol");
  return oxbox::utilities::AlignUp<16>(dimension);
}
unsigned Bitrate(unsigned width, unsigned height, unsigned kbps) {
  Expects(width > 0, "surface width is positive");
  Expects(height > 0, "surface height is positive");
  Expects(width <= 32766, "surface width fits the graphics protocol");
  Expects(height <= 32766, "surface height fits the graphics protocol");
  Expects(kbps <= UINT32_MAX / 1000, "bitrate fits NVENC");
  auto rate =
      kbps ? uint64_t(kbps) * 1000 : std::max(uint64_t(2000000), uint64_t(16000000) * width * height / (1920uz * 1080));
  return unsigned(std::clamp<uint64_t>(rate, 1, UINT32_MAX));
}
void ReplicateEdges(std::span<BYTE> pixels, unsigned width, unsigned height) {
  auto stride = Aligned(width) * 4;
  Expects(pixels.size() >= std::size_t(stride) * Aligned(height), "picture includes aligned storage");
  std::ranges::for_each(std::views::iota(0u, height), [&](unsigned row) {
    auto line = pixels.subspan(std::size_t(row) * stride, stride);
    auto edge = line.subspan(std::size_t(width - 1) * 4, 4);
    std::ranges::for_each(line.subspan(std::size_t(width) * 4) | std::views::chunk(4),
                          [&](auto pixel) { std::ranges::copy(edge, pixel.begin()); });
  });
  auto last = pixels.subspan(std::size_t(height - 1) * stride, stride);
  std::ranges::for_each(std::views::iota(height, Aligned(height)), [&](unsigned row) {
    std::ranges::copy(last, pixels.subspan(std::size_t(row) * stride, stride).begin());
  });
}
void Regions::Add(sdlrdp_rect area) {
  Expects(area.x >= 0, "region left edge is nonnegative");
  Expects(area.y >= 0, "region top edge is nonnegative");
  Expects(area.w > 0, "region width is positive");
  Expects(area.h > 0, "region height is positive");
  Expects(area.x + area.w <= 32766, "region right edge fits wire");
  Expects(area.y + area.h <= 32766, "region bottom edge fits wire");
  if (rects.empty())
    bounds = area;
  else {
    auto right  = std::max(bounds.x + bounds.w, area.x + area.w);
    auto bottom = std::max(bounds.y + bounds.h, area.y + area.h);
    bounds.x = std::min(bounds.x, area.x);
    bounds.y = std::min(bounds.y, area.y);
    bounds.w = right - bounds.x;
    bounds.h = bottom - bounds.y;
  }
  rects.push_back({ UINT16(area.x), UINT16(area.y), UINT16(area.x + area.w), UINT16(area.y + area.h) });
  quality.push_back({ 0x9a, 100, 26, 0, 1 });
}
struct Encoder::Impl {
public:
  bool                     Check(int status, char const* operation);
  bool                     Load();
  bool                     Session();
  bool                     Initialize(unsigned bitrate, unsigned fps);
  NV_ENC_INITIALIZE_PARAMS Parameters(unsigned fps, NV_ENC_CONFIG* config) const;
  bool                     Buffers();
  bool                     MinimumSize();
  NV_ENC_PIC_PARAMS        Picture(bool force_idr) const;
  bool                     Fill(std::span<BYTE const> bgrx, unsigned stride, Encoder& timing);
  void                     Close();

private:
  friend class                Encoder;
  CudaFunctions*              cuda    = nullptr;
  NvencFunctions*             loader  = nullptr;
  NV_ENCODE_API_FUNCTION_LIST api     { };
  CUdevice                    device  = 0;
  CUcontext                   context = nullptr;
  void*                       session = nullptr;
  NV_ENC_INPUT_PTR            input   = nullptr;
  NV_ENC_OUTPUT_PTR           output  = nullptr;
  unsigned                    width   = 0;
  unsigned                    height  = 0;
  unsigned                    w       = 0;
  unsigned                    h       = 0;
  bool                        small   = false;
  bool                        first   = true;
  std::string                 error;
};
bool Encoder::Impl::Check(int status, char const* operation) {
  Expects(operation, "operation name exists");
  if (!status) return true;
  error = std::format("AVC420 {} failed: {}", operation, status);
  WLog_ERR("sdlrdp.avc", "%s", error.c_str());
  return false;
}
bool Encoder::Impl::Load() {
  return Check(cuda_load_functions(&cuda, nullptr), "load libcuda.so.1") &&
         Check(nvenc_load_functions(&loader, nullptr), "load libnvidia-encode.so.1") &&
         Check(cuda->cuInit(0), "cuInit");
}
bool Encoder::Impl::Session() {
  Expects(cuda != nullptr, "CUDA library is loaded");
  Expects(loader != nullptr, "NVENC library is loaded");
  Expects(session == nullptr, "encoder session is fresh");
  Expects(context == nullptr, "CUDA context is fresh");
  api.version = NV_ENCODE_API_FUNCTION_LIST_VER;
  if (!Check(loader->NvEncodeAPICreateInstance(&api), "create API") ||
      !Check(cuda->cuDeviceGet(&device, 0), "cuDeviceGet") ||
      !Check(cuda->cuDevicePrimaryCtxRetain(&context, device), "retain CUDA context"))
    return false;
  NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{ };
  open.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
  open.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
  open.device     = context;
  open.apiVersion = NVENCAPI_VERSION;
  return Check(api.nvEncOpenEncodeSessionEx(&open, &session), "open session");
}
namespace {
void ConfigureRate(NV_ENC_RC_PARAMS& rc, unsigned bitrate, unsigned fps) {
  rc.enableLookahead  = 0;
  rc.lookaheadDepth   = 0;
  rc.rateControlMode  = NV_ENC_PARAMS_RC_CBR;
  rc.averageBitRate   = bitrate;
  rc.vbvBufferSize    = unsigned(std::clamp<uint64_t>(uint64_t(bitrate) * 2 / fps, 1, UINT32_MAX));
  rc.vbvInitialDelay  = rc.vbvBufferSize;
  rc.zeroReorderDelay = 1;
}
void ConfigureColour(NV_ENC_CONFIG_H264_VUI_PARAMETERS& vui) {
  vui.videoSignalTypePresentFlag   = 1;
  vui.videoFullRangeFlag           = 1;
  vui.colourDescriptionPresentFlag = 1;
  vui.colourPrimaries              = NV_ENC_VUI_COLOR_PRIMARIES_BT709;
  vui.transferCharacteristics      = NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709;
  vui.colourMatrix                 = NV_ENC_VUI_MATRIX_COEFFS_BT709;
}
void ConfigureH264(NV_ENC_CONFIG_H264& h264, unsigned fps) {
  h264.chromaFormatIDC = 1;
  h264.level           = NV_ENC_LEVEL_AUTOSELECT;
  h264.idrPeriod       = NVENC_INFINITE_GOPLENGTH;
  h264.repeatSPSPPS    = 1;
  auto refresh = IntraRefreshFor(fps);
  h264.enableIntraRefresh = 1;
  h264.intraRefreshPeriod = refresh.period;
  h264.intraRefreshCnt    = refresh.count;
  ConfigureColour(h264.h264VUIParameters);
}
void ConfigurePreset(NV_ENC_CONFIG& config, unsigned bitrate, unsigned fps) {
  config.profileGUID    = NV_ENC_H264_PROFILE_HIGH_GUID;
  config.gopLength      = NVENC_INFINITE_GOPLENGTH;
  config.frameIntervalP = 1;
  ConfigureRate(config.rcParams, bitrate, fps);
  ConfigureH264(config.encodeCodecConfig.h264Config, fps);
}
}
NV_ENC_INITIALIZE_PARAMS Encoder::Impl::Parameters(unsigned fps, NV_ENC_CONFIG* config) const {
  NV_ENC_INITIALIZE_PARAMS init{ };
  init.version           = NV_ENC_INITIALIZE_PARAMS_VER;
  init.encodeGUID        = NV_ENC_CODEC_H264_GUID;
  init.presetGUID        = NV_ENC_PRESET_P4_GUID;
  init.tuningInfo        = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
  init.encodeWidth       = init.maxEncodeWidth = w;
  init.encodeHeight      = init.maxEncodeHeight = h;
  init.darWidth          = width;
  init.darHeight         = height;
  init.frameRateNum      = fps;
  init.frameRateDen      = 1;
  init.enablePTD         = 1;
  init.enableEncodeAsync = 0;
  init.encodeConfig      = config;
  return init;
}
bool Encoder::Impl::Initialize(unsigned bitrate, unsigned fps) {
  Expects(session != nullptr, "encoder session exists");
  Expects(bitrate > 0, "encoder bitrate is positive");
  Expects(fps > 0, "encoder frame rate is positive");
  NV_ENC_PRESET_CONFIG preset{ };
  preset.version           = NV_ENC_PRESET_CONFIG_VER;
  preset.presetCfg.version = NV_ENC_CONFIG_VER;
  if (!Check(api.nvEncGetEncodePresetConfigEx(session, NV_ENC_CODEC_H264_GUID, NV_ENC_PRESET_P4_GUID,
                                              NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY, &preset),
             "preset"))
    return false;
  auto& config = preset.presetCfg;
  ConfigurePreset(config, bitrate, fps);
  auto init = Parameters(fps, &config);
  return Check(api.nvEncInitializeEncoder(session, &init), "initialize encoder");
}
bool Encoder::Impl::Buffers() {
  Expects(session != nullptr, "encoder session exists");
  Expects(w % 16 == 0, "encoder width is aligned");
  Expects(h % 16 == 0, "encoder height is aligned");
  NV_ENC_CREATE_INPUT_BUFFER in{ };
  in.version   = NV_ENC_CREATE_INPUT_BUFFER_VER;
  in.width     = w;
  in.height    = h;
  in.bufferFmt = NV_ENC_BUFFER_FORMAT_IYUV;
  if (!Check(api.nvEncCreateInputBuffer(session, &in), "create input buffer")) return false;
  input = in.inputBuffer;
  NV_ENC_CREATE_BITSTREAM_BUFFER out{ };
  out.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
  if (!Check(api.nvEncCreateBitstreamBuffer(session, &out), "create bitstream buffer")) return false;
  output = out.bitstreamBuffer;
  return true;
}
namespace {
int ConvertInput(NV_ENC_LOCK_INPUT_BUFFER const& lock, prim_size_t const& size, std::span<BYTE const> bgrx,
                 unsigned stride) {
  Expects(lock.pitch >= size.width, "I420 pitch covers aligned width");
  Expects(lock.pitch % 2 == 0, "I420 pitch is even");
  auto* y{ static_cast<BYTE*>(lock.bufferDataPtr) };
  std::array<BYTE*, 3> planes{ y, y + (std::size_t(lock.pitch) * size.height),
                               y + (std::size_t(lock.pitch) * size.height * 5 / 4) };
  std::array<UINT32, 3> pitches{ lock.pitch, lock.pitch / 2, lock.pitch / 2 };
  return primitives_get()->RGBToYUV420_8u_P3AC4R(bgrx.data(), PIXEL_FORMAT_BGRX32, stride, planes.data(),
                                                 pitches.data(), &size);
}
}
bool Encoder::Impl::Fill(std::span<BYTE const> bgrx, unsigned stride, Encoder& timing) {
  Expects(session != nullptr, "encoder session exists");
  Expects(input != nullptr, "encoder input buffer exists");
  using Clock = std::chrono::steady_clock;
  auto                     start = Clock::now();
  NV_ENC_LOCK_INPUT_BUFFER lock  { };
  lock.version     = NV_ENC_LOCK_INPUT_BUFFER_VER;
  lock.inputBuffer = input;
  if (!Check(api.nvEncLockInputBuffer(session, &lock), "lock input")) return false;
  timing.times.upload = Clock::now() - start;
  start               = Clock::now();
  auto status = ConvertInput(lock, { w, h }, bgrx, stride);
  timing.times.convert = Clock::now() - start;
  start                = Clock::now();
  auto unlocked = Check(api.nvEncUnlockInputBuffer(session, input), "unlock input");
  timing.times.upload += Clock::now() - start;
  return Check(status, "BT.709 conversion") && unlocked;
}
void Encoder::Impl::Close() {
  if (input) Check(api.nvEncDestroyInputBuffer(session, input), "destroy input");
  if (output) Check(api.nvEncDestroyBitstreamBuffer(session, output), "destroy bitstream");
  if (session) Check(api.nvEncDestroyEncoder(session), "destroy session");
  if (context) Check(cuda->cuDevicePrimaryCtxRelease(device), "release CUDA context");
  input   = nullptr;
  output  = nullptr;
  session = nullptr;
  context = nullptr;
  nvenc_free_functions(&loader);
  cuda_free_functions(&cuda);
  first = true;
}
Encoder::Encoder() : impl(std::make_unique<Impl>()) { }
Encoder::~Encoder() {
  Close();
}
void Encoder::Close() {
  impl->Close();
}
bool Encoder::IsOpen() const {
  return impl->output != nullptr;
}
bool Encoder::TooSmall() const {
  return impl->small;
}
std::string const& Encoder::Error() const {
  return impl->error;
}
std::string Encoder::UnavailableReason() {
  static std::string const reason = [] {
    Impl probe;
    auto available = probe.Load();
    auto error     = probe.error;
    probe.Close();
    return available ? std::string{ } : error;
  }();
  return reason;
}
bool Encoder::Available() {
  return UnavailableReason().empty();
}
bool Encoder::Impl::MinimumSize() {
  NV_ENC_CAPS_PARAM caps{ };
  caps.version = NV_ENC_CAPS_PARAM_VER;
  int min_width  = 0;
  int min_height = 0;
  caps.capsToQuery = NV_ENC_CAPS_WIDTH_MIN;
  bool ok = Check(api.nvEncGetEncodeCaps(session, NV_ENC_CODEC_H264_GUID, &caps, &min_width), "minimum width");
  caps.capsToQuery = NV_ENC_CAPS_HEIGHT_MIN;
  ok = Check(api.nvEncGetEncodeCaps(session, NV_ENC_CODEC_H264_GUID, &caps, &min_height), "minimum height") && ok;
  small            = ok && (std::cmp_less(w, min_width) || std::cmp_less(h, min_height));
  if (small) error = "surface below NVENC minimum picture size";
  return ok;
}
bool Encoder::Open(unsigned width, unsigned height, unsigned bitrate, unsigned fps) {
  Expects(width > 0, "picture width is positive");
  Expects(height > 0, "picture height is positive");
  Expects(bitrate > 0, "encoder bitrate is positive");
  Expects(fps > 0, "encoder frame rate is positive");
  Close();
  impl->error.clear();
  impl->small  = false;
  impl->width  = width;
  impl->height = height;
  impl->w      = Aligned(width);
  impl->h      = Aligned(height);
  if (!impl->Load() || !impl->Session() || !impl->MinimumSize() || impl->small || !impl->Initialize(bitrate, fps) ||
      !impl->Buffers()) {
    Close();
    return false;
  }
  return true;
}
NV_ENC_PIC_PARAMS Encoder::Impl::Picture(bool force_idr) const {
  NV_ENC_PIC_PARAMS pic{ };
  pic.version         = NV_ENC_PIC_PARAMS_VER;
  pic.inputBuffer     = input;
  pic.outputBitstream = output;
  pic.bufferFmt       = NV_ENC_BUFFER_FORMAT_IYUV;
  pic.pictureStruct   = NV_ENC_PIC_STRUCT_FRAME;
  pic.inputWidth      = w;
  pic.inputHeight     = h;
  if (force_idr || first) pic.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
  return pic;
}
std::span<BYTE const> Encoder::Encode(std::span<BYTE const> bgrx, unsigned stride, bool force_idr,
                                      std::vector<BYTE>& encoded) {
  Expects(IsOpen(), "encoder is open");
  Expects(stride >= impl->w * 4, "source stride covers aligned width");
  Expects(bgrx.size() >= std::size_t(impl->h - 1) * stride + std::size_t(impl->w) * 4, "source covers aligned height");
  times.convert = times.upload = times.encode = { };
  if (!impl->Fill(bgrx, stride, *this)) return { };
  auto pic   = impl->Picture(force_idr);
  auto start = std::chrono::steady_clock::now();
  if (!impl->Check(impl->api.nvEncEncodePicture(impl->session, &pic), "encode picture")) return { };
  NV_ENC_LOCK_BITSTREAM lock{ .version = NV_ENC_LOCK_BITSTREAM_VER, .outputBitstream = impl->output };
  if (!impl->Check(impl->api.nvEncLockBitstream(impl->session, &lock), "lock bitstream")) return { };
  times.encode = std::chrono::steady_clock::now() - start;
  auto const* data = static_cast<BYTE const*>(lock.bitstreamBufferPtr);
  encoded.assign(data, data + lock.bitstreamSizeInBytes);
  if (!impl->Check(impl->api.nvEncUnlockBitstream(impl->session, impl->output), "unlock bitstream")) return { };
  impl->first = false;
  Ensures(!encoded.empty(), "one access unit produced synchronously");
  return encoded;
}
} // namespace Backend::Avc
