#include "_detail/avc.hpp"
#include <winpr/wlog.h>
#define FFNV_LOG_FUNC(ctx, msg, ...) WLog_ERR("sdlrdp.avc", msg, __VA_ARGS__)
#define FFNV_DEBUG_LOG_FUNC(ctx, msg, ...) ((void)0)
// WinPR already supplies the ABI-compatible GUID type.
#define GUID_DEFINED
#include <ffnvcodec/dynlink_loader.h>
#include <ranges>
#include <freerdp/primitives.h>
#include <format>
#include <oxbox/utilities/bits.hpp>

namespace Backend::Avc {
using utilities::Expects;
using utilities::Ensures;
IntraRefresh IntraRefreshFor(unsigned fps)
{
  Expects(fps && fps <= UINT32_MAX / 2, "refresh period fits NVENC");
  // Recovery target: refresh every two seconds, spreading each sweep over half a second.
  IntraRefresh refresh{2 * fps, std::max(1u, fps / 2)};
  Ensures(refresh.count <= refresh.period, "refresh sweep fits its period");
  return refresh;
}
unsigned Aligned(unsigned dimension)
{
  Expects(dimension > 0, "surface dimension is positive");
  Expects(dimension <= 32766, "surface dimension fits the graphics protocol");
  return oxbox::utilities::AlignUp<16>(dimension);
}
unsigned Bitrate(unsigned width, unsigned height, unsigned kbps)
{
  Expects(width > 0, "surface width is positive");
  Expects(height > 0, "surface height is positive");
  Expects(width <= 32766, "surface width fits the graphics protocol");
  Expects(height <= 32766, "surface height fits the graphics protocol");
  Expects(kbps <= UINT32_MAX / 1000, "bitrate fits NVENC");
  auto rate = kbps ? uint64_t(kbps) * 1000 : std::max(uint64_t(2000000), uint64_t(16000000) * width * height / (1920 * 1080));
  return unsigned(std::clamp<uint64_t>(rate, 1, UINT32_MAX));
}
void ReplicateEdges(std::span<BYTE> pixels, unsigned width, unsigned height)
{
  auto stride = Aligned(width) * 4;
  Expects(pixels.size() >= std::size_t(stride) * Aligned(height), "picture includes aligned storage");
  std::ranges::for_each(std::views::iota(0u, height), [&](unsigned row) {
    auto line = pixels.subspan(std::size_t(row) * stride, stride);
    auto edge = line.subspan((width - 1) * 4, 4);
    std::ranges::for_each(line.subspan(width * 4) | std::views::chunk(4), [&](auto pixel) {
      std::ranges::copy(edge, pixel.begin());
    });
  });
  auto last = pixels.subspan(std::size_t(height - 1) * stride, stride);
  std::ranges::for_each(std::views::iota(height, Aligned(height)), [&](unsigned row) {
    std::ranges::copy(last, pixels.subspan(std::size_t(row) * stride, stride).begin());
  });
}
void Regions::Add(sdlrdp_rect area)
{
  Expects(area.x >= 0, "region left edge is nonnegative");
  Expects(area.y >= 0, "region top edge is nonnegative");
  Expects(area.w > 0, "region width is positive");
  Expects(area.h > 0, "region height is positive");
  Expects(area.x + area.w <= 32766, "region right edge fits wire");
  Expects(area.y + area.h <= 32766, "region bottom edge fits wire");
  if (rects.empty()) bounds = area;
  else {
    auto right = std::max(bounds.x + bounds.w, area.x + area.w);
    auto bottom = std::max(bounds.y + bounds.h, area.y + area.h);
    bounds.x = std::min(bounds.x, area.x); bounds.y = std::min(bounds.y, area.y);
    bounds.w = right - bounds.x; bounds.h = bottom - bounds.y;
  }
  rects.push_back({UINT16(area.x), UINT16(area.y), UINT16(area.x + area.w), UINT16(area.y + area.h)});
  quality.push_back({0x9a, 100, 26, 0, 1});
}
struct Encoder::Impl {
  CudaFunctions* cuda = nullptr;
  NvencFunctions* loader = nullptr;
  NV_ENCODE_API_FUNCTION_LIST api{};
  CUdevice device = 0;
  CUcontext context = nullptr;
  void* session = nullptr;
  NV_ENC_INPUT_PTR input = nullptr;
  NV_ENC_OUTPUT_PTR output = nullptr;
  unsigned width = 0, height = 0, w = 0, h = 0;
  bool small = false, first = true;
  std::string error;
  bool Check(int status, char const* operation);
  bool Load();
  bool Session();
  bool Initialize(unsigned bitrate, unsigned fps);
  bool Buffers();
  bool Fill(std::span<BYTE const> bgrx, unsigned stride, Encoder& timing);
  void Close();
};
bool Encoder::Impl::Check(int status, char const* operation)
{
  Expects(operation, "operation name exists");
  if (!status) return true;
  error = std::format("AVC420 {} failed: {}", operation, status);
  WLog_ERR("sdlrdp.avc", "%s", error.c_str());
  return false;
}
bool Encoder::Impl::Load()
{
  return Check(cuda_load_functions(&cuda, nullptr), "load libcuda.so.1")
    && Check(nvenc_load_functions(&loader, nullptr), "load libnvidia-encode.so.1")
    && Check(cuda->cuInit(0), "cuInit");
}
bool Encoder::Impl::Session()
{
  Expects(cuda != nullptr, "CUDA library is loaded");
  Expects(loader != nullptr, "NVENC library is loaded");
  Expects(session == nullptr, "encoder session is fresh");
  Expects(context == nullptr, "CUDA context is fresh");
  api.version = NV_ENCODE_API_FUNCTION_LIST_VER;
  if (!Check(loader->NvEncodeAPICreateInstance(&api), "create API")
      || !Check(cuda->cuDeviceGet(&device, 0), "cuDeviceGet")
      || !Check(cuda->cuDevicePrimaryCtxRetain(&context, device), "retain CUDA context")) return false;
  NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{};
  open.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
  open.deviceType = NV_ENC_DEVICE_TYPE_CUDA; open.device = context; open.apiVersion = NVENCAPI_VERSION;
  return Check(api.nvEncOpenEncodeSessionEx(&open, &session), "open session");
}
bool Encoder::Impl::Initialize(unsigned bitrate, unsigned fps)
{
  Expects(session != nullptr, "encoder session exists");
  Expects(bitrate > 0, "encoder bitrate is positive");
  Expects(fps > 0, "encoder frame rate is positive");
  NV_ENC_PRESET_CONFIG preset{};
  preset.version = NV_ENC_PRESET_CONFIG_VER; preset.presetCfg.version = NV_ENC_CONFIG_VER;
  if (!Check(api.nvEncGetEncodePresetConfigEx(session, NV_ENC_CODEC_H264_GUID, NV_ENC_PRESET_P4_GUID,
      NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY, &preset), "preset")) return false;
  auto& config = preset.presetCfg;
  config.profileGUID = NV_ENC_H264_PROFILE_HIGH_GUID;
  config.gopLength = NVENC_INFINITE_GOPLENGTH; config.frameIntervalP = 1;
  auto& rc = config.rcParams;
  rc.enableLookahead = 0; rc.lookaheadDepth = 0;
  rc.rateControlMode = NV_ENC_PARAMS_RC_CBR; rc.averageBitRate = bitrate;
  rc.vbvBufferSize = unsigned(std::clamp<uint64_t>(uint64_t(bitrate) * 2 / fps, 1, UINT32_MAX));
  rc.vbvInitialDelay = rc.vbvBufferSize; rc.zeroReorderDelay = 1;
  auto& h264 = config.encodeCodecConfig.h264Config;
  h264.chromaFormatIDC = 1; h264.level = NV_ENC_LEVEL_AUTOSELECT;
  h264.idrPeriod = NVENC_INFINITE_GOPLENGTH; h264.repeatSPSPPS = 1;
  auto refresh = IntraRefreshFor(fps);
  h264.enableIntraRefresh  = 1;
  h264.intraRefreshPeriod  = refresh.period;
  h264.intraRefreshCnt     = refresh.count;
  auto& vui = h264.h264VUIParameters;
  vui.videoSignalTypePresentFlag = 1; vui.videoFullRangeFlag = 1; vui.colourDescriptionPresentFlag = 1;
  vui.colourPrimaries = NV_ENC_VUI_COLOR_PRIMARIES_BT709;
  vui.transferCharacteristics = NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709;
  vui.colourMatrix = NV_ENC_VUI_MATRIX_COEFFS_BT709;
  NV_ENC_INITIALIZE_PARAMS init{};
  init.version = NV_ENC_INITIALIZE_PARAMS_VER; init.encodeGUID = NV_ENC_CODEC_H264_GUID;
  init.presetGUID = NV_ENC_PRESET_P4_GUID; init.tuningInfo = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
  init.encodeWidth = init.maxEncodeWidth = w; init.encodeHeight = init.maxEncodeHeight = h;
  init.darWidth = width; init.darHeight = height; init.frameRateNum = fps; init.frameRateDen = 1;
  init.enablePTD = 1; init.enableEncodeAsync = 0; init.encodeConfig = &config;
  return Check(api.nvEncInitializeEncoder(session, &init), "initialize encoder");
}
bool Encoder::Impl::Buffers()
{
  Expects(session != nullptr, "encoder session exists");
  Expects(w % 16 == 0, "encoder width is aligned");
  Expects(h % 16 == 0, "encoder height is aligned");
  NV_ENC_CREATE_INPUT_BUFFER in{};
  in.version = NV_ENC_CREATE_INPUT_BUFFER_VER; in.width = w; in.height = h;
  in.bufferFmt = NV_ENC_BUFFER_FORMAT_IYUV;
  if (!Check(api.nvEncCreateInputBuffer(session, &in), "create input buffer")) return false;
  input = in.inputBuffer;
  NV_ENC_CREATE_BITSTREAM_BUFFER out{}; out.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
  if (!Check(api.nvEncCreateBitstreamBuffer(session, &out), "create bitstream buffer")) return false;
  output = out.bitstreamBuffer;
  return true;
}
bool Encoder::Impl::Fill(std::span<BYTE const> bgrx, unsigned stride, Encoder& timing)
{
  Expects(session != nullptr, "encoder session exists");
  Expects(input != nullptr, "encoder input buffer exists");
  using Clock = std::chrono::steady_clock;
  auto start = Clock::now();
  NV_ENC_LOCK_INPUT_BUFFER lock{};
  lock.version = NV_ENC_LOCK_INPUT_BUFFER_VER; lock.inputBuffer = input;
  if (!Check(api.nvEncLockInputBuffer(session, &lock), "lock input")) return false;
  Expects(lock.pitch >= w, "I420 pitch covers aligned width");
  Expects(lock.pitch % 2 == 0, "I420 pitch is even");
  auto        y         { static_cast<BYTE*>(lock.bufferDataPtr)                                      };
  BYTE*       planes [] { y, y + std::size_t(lock.pitch) * h, y + std::size_t(lock.pitch) * h * 5 / 4 };
  UINT32      pitches[] { lock.pitch, lock.pitch / 2, lock.pitch / 2                                  };
  prim_size_t size      { w, h                                                                        };
  timing.upload_time = Clock::now() - start;
  start = Clock::now();
  auto status = primitives_get()->RGBToYUV420_8u_P3AC4R(bgrx.data(), PIXEL_FORMAT_BGRX32, stride, planes, pitches, &size);
  timing.convert_time = Clock::now() - start;
  start = Clock::now();
  auto unlocked = Check(api.nvEncUnlockInputBuffer(session, input), "unlock input");
  timing.upload_time += Clock::now() - start;
  return Check(status, "BT.709 conversion") && unlocked;
}
void Encoder::Impl::Close()
{
  if (input) Check(api.nvEncDestroyInputBuffer(session, input), "destroy input");
  if (output) Check(api.nvEncDestroyBitstreamBuffer(session, output), "destroy bitstream");
  if (session) Check(api.nvEncDestroyEncoder(session), "destroy session");
  if (context) Check(cuda->cuDevicePrimaryCtxRelease(device), "release CUDA context");
  input = nullptr; output = nullptr; session = nullptr; context = nullptr;
  nvenc_free_functions(&loader); cuda_free_functions(&cuda);
  first = true;
}
Encoder::Encoder() : impl(std::make_unique<Impl>()) {}
Encoder::~Encoder() { Close(); }
void Encoder::Close() { impl->Close(); }
bool Encoder::IsOpen() const { return impl->output != nullptr; }
bool Encoder::TooSmall() const { return impl->small; }
std::string const& Encoder::Error() const { return impl->error; }
std::string Encoder::UnavailableReason()
{
  static std::string const reason = [] {
    Impl probe;
    auto available = probe.Load();
    auto error = probe.error;
    probe.Close();
    return available ? std::string{} : error;
  }();
  return reason;
}
bool Encoder::Available() { return UnavailableReason().empty(); }
bool Encoder::Open(unsigned width, unsigned height, unsigned bitrate, unsigned fps)
{
  Expects(width > 0, "picture width is positive");
  Expects(height > 0, "picture height is positive");
  Expects(bitrate > 0, "encoder bitrate is positive");
  Expects(fps > 0, "encoder frame rate is positive");
  Close(); impl->error.clear(); impl->small = false;
  impl->width = width; impl->height = height; impl->w = Aligned(width); impl->h = Aligned(height);
  if (!impl->Load() || !impl->Session()) { Close(); return false; }
  NV_ENC_CAPS_PARAM caps{}; caps.version = NV_ENC_CAPS_PARAM_VER;
  int min_width = 0, min_height = 0;
  caps.capsToQuery = NV_ENC_CAPS_WIDTH_MIN;
  bool ok = impl->Check(impl->api.nvEncGetEncodeCaps(impl->session, NV_ENC_CODEC_H264_GUID, &caps, &min_width), "minimum width");
  caps.capsToQuery = NV_ENC_CAPS_HEIGHT_MIN;
  ok = impl->Check(impl->api.nvEncGetEncodeCaps(impl->session, NV_ENC_CODEC_H264_GUID, &caps, &min_height), "minimum height") && ok;
  impl->small = ok && (impl->w < unsigned(min_width) || impl->h < unsigned(min_height));
  if (impl->small) impl->error = "surface below NVENC minimum picture size";
  if (!ok || impl->small || !impl->Initialize(bitrate, fps) || !impl->Buffers()) { Close(); return false; }
  return true;
}
std::span<BYTE const> Encoder::Encode(std::span<BYTE const> bgrx, unsigned stride, bool force_idr, std::vector<BYTE>& encoded)
{
  Expects(IsOpen(), "encoder is open");
  Expects(stride >= impl->w * 4, "source stride covers aligned width");
  Expects(bgrx.size() >= std::size_t(impl->h - 1) * stride + impl->w * 4, "source covers aligned height");
  convert_time = upload_time = encode_time = {};
  if (!impl->Fill(bgrx, stride, *this)) return {};
  NV_ENC_PIC_PARAMS pic{}; pic.version = NV_ENC_PIC_PARAMS_VER;
  pic.inputBuffer = impl->input; pic.outputBitstream = impl->output;
  pic.bufferFmt = NV_ENC_BUFFER_FORMAT_IYUV; pic.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
  pic.inputWidth = impl->w; pic.inputHeight = impl->h;
  if (force_idr || impl->first) pic.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
  auto start = std::chrono::steady_clock::now();
  if (!impl->Check(impl->api.nvEncEncodePicture(impl->session, &pic), "encode picture")) return {};
  NV_ENC_LOCK_BITSTREAM lock{}; lock.version = NV_ENC_LOCK_BITSTREAM_VER; lock.outputBitstream = impl->output;
  if (!impl->Check(impl->api.nvEncLockBitstream(impl->session, &lock), "lock bitstream")) return {};
  encode_time = std::chrono::steady_clock::now() - start;
  auto data = static_cast<BYTE const*>(lock.bitstreamBufferPtr);
  encoded.assign(data, data + lock.bitstreamSizeInBytes);
  if (!impl->Check(impl->api.nvEncUnlockBitstream(impl->session, impl->output), "unlock bitstream")) return {};
  impl->first = false;
  Ensures(!encoded.empty(), "one access unit produced synchronously");
  return encoded;
}
}
