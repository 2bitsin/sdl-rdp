#include "_detail/avc.hpp"
#include <winpr/wlog.h>
#define FFNV_LOG_FUNC(ctx, msg, ...) WLog_ERR("sdlrdp.avc", msg, __VA_ARGS__)
#define FFNV_DEBUG_LOG_FUNC(ctx, msg, ...) ((void)0)
// WinPR already supplies the ABI-compatible GUID type.
#define GUID_DEFINED
#include <ffnvcodec/dynlink_loader.h>
#include <freerdp/primitives.h>
#include <freerdp/codec/color.h>
#include <cstring>
#include <format>
#include <oxbox/utilities/bits.hpp>

namespace Backend::Avc {
using utilities::Expects;
using utilities::Ensures;
unsigned Aligned(unsigned dimension)
{
  Expects(dimension && dimension <= 32766, "surface dimension fits the graphics protocol");
  return oxbox::utilities::AlignUp<16>(dimension);
}
unsigned Bitrate(unsigned width, unsigned height, unsigned kbps)
{
  Expects(width && height && width <= 32766 && height <= 32766, "nonempty graphics surface");
  Expects(kbps <= UINT32_MAX / 1000, "bitrate fits NVENC");
  auto rate = kbps ? uint64_t(kbps) * 1000 : std::max(uint64_t(2000000), uint64_t(16000000) * width * height / (1920 * 1080));
  return unsigned(std::clamp<uint64_t>(rate, 1, UINT32_MAX));
}
void Pad(std::span<BYTE const> pixels, unsigned stride, unsigned width, unsigned height, std::vector<BYTE>& padded)
{
  Expects(width && height && stride >= width * 4 && pixels.size() >= std::size_t(height - 1) * stride + width * 4,
    "source rows contain the whole BGRX picture");
  auto w = Aligned(width), h = Aligned(height);
  padded.resize(std::size_t(w) * h * 4);
  for (unsigned y = 0; y < h; ++y) {
    auto src = pixels.data() + std::size_t(std::min(y, height - 1)) * stride;
    auto dst = padded.data() + std::size_t(y) * w * 4;
    std::memcpy(dst, src, width * 4);
    for (unsigned x = width; x < w; ++x) std::memcpy(dst + x * 4, src + (width - 1) * 4, 4);
  }
  Ensures(w % 16 == 0 && h % 16 == 0, "encoded picture is macroblock aligned");
}
void Regions::Add(sdlrdp_rect area)
{
  Expects(area.x >= 0 && area.y >= 0 && area.w > 0 && area.h > 0
    && area.x + area.w <= 32766 && area.y + area.h <= 32766, "nonempty wire rectangle fits");
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
  std::vector<BYTE> encoded, padded;
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
  Expects(cuda && loader && !session && !context, "loaded libraries and fresh session");
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
  Expects(session && bitrate && fps, "session and rate configured");
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
  rc.vbvBufferSize = std::max(1u, bitrate / fps); rc.vbvInitialDelay = rc.vbvBufferSize; rc.zeroReorderDelay = 1;
  auto& h264 = config.encodeCodecConfig.h264Config;
  h264.chromaFormatIDC = 1; h264.level = NV_ENC_LEVEL_AUTOSELECT;
  h264.idrPeriod = NVENC_INFINITE_GOPLENGTH; h264.repeatSPSPPS = 1;
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
  Expects(session && w % 16 == 0 && h % 16 == 0, "initialized aligned encoder");
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
  Expects(session && input, "encoder owns input buffer");
  using Clock = std::chrono::steady_clock;
  auto start = Clock::now();
  auto pixels = bgrx.data();
  if (width != w || height != h) {
    Pad(bgrx, stride, width, height, padded);
    pixels = padded.data(); stride = w * 4;
  }
  timing.convert_time = Clock::now() - start;
  start = Clock::now();
  NV_ENC_LOCK_INPUT_BUFFER lock{};
  lock.version = NV_ENC_LOCK_INPUT_BUFFER_VER; lock.inputBuffer = input;
  if (!Check(api.nvEncLockInputBuffer(session, &lock), "lock input")) return false;
  Expects(lock.pitch >= w && lock.pitch % 2 == 0, "I420 pitch fits aligned rows");
  auto y = static_cast<BYTE*>(lock.bufferDataPtr);
  BYTE* planes[]{y, y + std::size_t(lock.pitch) * h, y + std::size_t(lock.pitch) * h * 5 / 4};
  UINT32 pitches[]{lock.pitch, lock.pitch / 2, lock.pitch / 2};
  timing.upload_time = Clock::now() - start;
  start = Clock::now();
  prim_size_t size{w, h};
  auto status = primitives_get()->RGBToYUV420_8u_P3AC4R(pixels, PIXEL_FORMAT_BGRX32, stride, planes, pitches, &size);
  timing.convert_time += Clock::now() - start;
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
  Expects(width && height && bitrate && fps, "picture and rate are nonzero");
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
std::span<BYTE const> Encoder::Encode(std::span<BYTE const> bgrx, unsigned stride, bool force_idr)
{
  Expects(IsOpen(), "encoder is open");
  Expects(stride >= impl->width * 4 && bgrx.size() >= std::size_t(impl->height - 1) * stride + impl->width * 4,
    "source contains the whole BGRX picture");
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
  impl->encoded.assign(data, data + lock.bitstreamSizeInBytes);
  if (!impl->Check(impl->api.nvEncUnlockBitstream(impl->session, impl->output), "unlock bitstream")) return {};
  impl->first = false;
  Ensures(!impl->encoded.empty(), "one access unit produced synchronously");
  return impl->encoded;
}
}
