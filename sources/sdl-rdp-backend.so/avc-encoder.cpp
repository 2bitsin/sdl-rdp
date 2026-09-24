#include "_detail/avc-encoder.hpp"
#include "_detail/avc.hpp"
#include "_detail/contract.hpp"

#include <algorithm>
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
#include <ranges>
#include <utility>

namespace Backend::Avc {
using utilities::Ensures;
using utilities::Expects;
struct Encoder::Impl {
public:
  auto Check(int status, char const* operation)                           -> bool;
  auto Load()                                                             -> bool;
  auto Session()                                                          -> bool;
  auto Initialize(unsigned bitrate, unsigned fps)                         -> bool;
  auto Parameters(unsigned fps, NV_ENC_CONFIG* config) const              -> NV_ENC_INITIALIZE_PARAMS;
  auto Buffers()                                                          -> bool;
  auto Capability(NV_ENC_CAPS query, int& value, char const* operation)   -> bool;
  auto MinimumSize()                                                      -> bool;
  auto Picture(bool force_idr) const                                      -> NV_ENC_PIC_PARAMS;
  auto Fill(std::span<BYTE const> bgrx, unsigned stride, Encoder& timing) -> bool;
  auto Close()                                                            -> void;

private:
  friend class Encoder;
  struct Driver {
    CudaFunctions*              cuda    = nullptr;
    NvencFunctions*             loader  = nullptr;
    NV_ENCODE_API_FUNCTION_LIST api     { };
    CUdevice                    device  = 0;
    CUcontext                   context = nullptr;
  };
  struct Handles {
    void*             session = nullptr;
    NV_ENC_INPUT_PTR  input   = nullptr;
    NV_ENC_OUTPUT_PTR output  = nullptr;
  };
  Driver      driver;
  Handles     handles;
  Extent      picture;
  Extent      aligned;
  bool        small   = false;
  bool        first   = true;
  std::string error;
};
auto Encoder::Impl::Check(int status, char const* operation) -> bool {
  Expects(operation, "operation name exists");
  if (!status) return true;
  error = std::format("AVC420 {} failed: {}", operation, status);
  WLog_ERR("sdlrdp.avc", "%s", error.c_str());
  return false;
}
auto Encoder::Impl::Load() -> bool {
  return Check(cuda_load_functions(&driver.cuda, nullptr), "load libcuda.so.1") &&
         Check(nvenc_load_functions(&driver.loader, nullptr), "load libnvidia-encode.so.1") &&
         Check(driver.cuda->cuInit(0), "cuInit");
}
auto Encoder::Impl::Session() -> bool {
  Expects(driver.cuda != nullptr, "CUDA library is loaded");
  Expects(driver.loader != nullptr, "NVENC library is loaded");
  Expects(handles.session == nullptr, "encoder session is fresh");
  Expects(driver.context == nullptr, "CUDA context is fresh");
  driver.api.version = NV_ENCODE_API_FUNCTION_LIST_VER;
  if (!Check(driver.loader->NvEncodeAPICreateInstance(&driver.api), "create API") ||
      !Check(driver.cuda->cuDeviceGet(&driver.device, 0), "cuDeviceGet") ||
      !Check(driver.cuda->cuDevicePrimaryCtxRetain(&driver.context, driver.device), "retain CUDA context"))
    return false;
  NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{ };
  open.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
  open.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
  open.device     = driver.context;
  open.apiVersion = NVENCAPI_VERSION;
  return Check(driver.api.nvEncOpenEncodeSessionEx(&open, &handles.session), "open session");
}
namespace {
auto ConfigureRate(NV_ENC_RC_PARAMS& rc, unsigned bitrate, unsigned fps) -> void {
  rc.enableLookahead  = 0;
  rc.lookaheadDepth   = 0;
  rc.rateControlMode  = NV_ENC_PARAMS_RC_CBR;
  rc.averageBitRate   = bitrate;
  rc.vbvBufferSize    = unsigned(std::clamp<uint64_t>(uint64_t(bitrate) * 2 / fps, 1, UINT32_MAX));
  rc.vbvInitialDelay  = rc.vbvBufferSize;
  rc.zeroReorderDelay = 1;
}
auto ConfigureColour(NV_ENC_CONFIG_H264_VUI_PARAMETERS& vui) -> void {
  vui.videoSignalTypePresentFlag   = 1;
  vui.videoFullRangeFlag           = 1;
  vui.colourDescriptionPresentFlag = 1;
  vui.colourPrimaries              = NV_ENC_VUI_COLOR_PRIMARIES_BT709;
  vui.transferCharacteristics      = NV_ENC_VUI_TRANSFER_CHARACTERISTIC_BT709;
  vui.colourMatrix                 = NV_ENC_VUI_MATRIX_COEFFS_BT709;
}
auto ConfigureH264(NV_ENC_CONFIG_H264& h264, unsigned fps) -> void {
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
auto ConfigurePreset(NV_ENC_CONFIG& config, unsigned bitrate, unsigned fps) -> void {
  config.profileGUID    = NV_ENC_H264_PROFILE_HIGH_GUID;
  config.gopLength      = NVENC_INFINITE_GOPLENGTH;
  config.frameIntervalP = 1;
  ConfigureRate(config.rcParams, bitrate, fps);
  ConfigureH264(config.encodeCodecConfig.h264Config, fps);
}
}
auto Encoder::Impl::Parameters(unsigned fps, NV_ENC_CONFIG* config) const -> NV_ENC_INITIALIZE_PARAMS {
  NV_ENC_INITIALIZE_PARAMS init{ };
  init.version           = NV_ENC_INITIALIZE_PARAMS_VER;
  init.encodeGUID        = NV_ENC_CODEC_H264_GUID;
  init.presetGUID        = NV_ENC_PRESET_P4_GUID;
  init.tuningInfo        = NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY;
  init.encodeWidth       = init.maxEncodeWidth = aligned.width;
  init.encodeHeight      = init.maxEncodeHeight = aligned.height;
  init.darWidth          = picture.width;
  init.darHeight         = picture.height;
  init.frameRateNum      = fps;
  init.frameRateDen      = 1;
  init.enablePTD         = 1;
  init.enableEncodeAsync = 0;
  init.encodeConfig      = config;
  return init;
}
auto Encoder::Impl::Initialize(unsigned bitrate, unsigned fps) -> bool {
  Expects(handles.session != nullptr, "encoder session exists");
  Expects(bitrate > 0, "encoder bitrate is positive");
  Expects(fps > 0, "encoder frame rate is positive");
  NV_ENC_PRESET_CONFIG preset{ };
  preset.version           = NV_ENC_PRESET_CONFIG_VER;
  preset.presetCfg.version = NV_ENC_CONFIG_VER;
  if (!Check(driver.api.nvEncGetEncodePresetConfigEx(handles.session, NV_ENC_CODEC_H264_GUID, NV_ENC_PRESET_P4_GUID,
                                                     NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY, &preset),
             "preset"))
    return false;
  auto& config = preset.presetCfg;
  ConfigurePreset(config, bitrate, fps);
  auto init = Parameters(fps, &config);
  return Check(driver.api.nvEncInitializeEncoder(handles.session, &init), "initialize encoder");
}
auto Encoder::Impl::Buffers() -> bool {
  Expects(handles.session != nullptr, "encoder session exists");
  Expects(aligned.width % 16 == 0, "encoder width is aligned");
  Expects(aligned.height % 16 == 0, "encoder height is aligned");
  NV_ENC_CREATE_INPUT_BUFFER in{ };
  in.version   = NV_ENC_CREATE_INPUT_BUFFER_VER;
  in.width     = aligned.width;
  in.height    = aligned.height;
  in.bufferFmt = NV_ENC_BUFFER_FORMAT_IYUV;
  if (!Check(driver.api.nvEncCreateInputBuffer(handles.session, &in), "create input buffer")) return false;
  handles.input = in.inputBuffer;
  NV_ENC_CREATE_BITSTREAM_BUFFER out{ };
  out.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
  if (!Check(driver.api.nvEncCreateBitstreamBuffer(handles.session, &out), "create bitstream buffer")) return false;
  handles.output = out.bitstreamBuffer;
  return true;
}
namespace {
auto ConvertInput(NV_ENC_LOCK_INPUT_BUFFER const& lock, prim_size_t const& size, std::span<BYTE const> bgrx,
                  unsigned stride) -> int {
  Expects(lock.pitch >= size.width, "I420 pitch covers aligned width");
  Expects(lock.pitch % 2 == 0, "I420 pitch is even");
  auto*                 y      { static_cast<BYTE*>(lock.bufferDataPtr)     };
  std::array<BYTE*, 3>  planes { y, y + (std::size_t(lock.pitch) * size.height),
                                 y + (std::size_t(lock.pitch) * size.height * 5 / 4) };
  std::array<UINT32, 3> pitches{ lock.pitch, lock.pitch / 2, lock.pitch / 2 };
  return primitives_get()->RGBToYUV420_8u_P3AC4R(bgrx.data(), PIXEL_FORMAT_BGRX32, stride, planes.data(),
                                                 pitches.data(), &size);
}
}
auto Encoder::Impl::Fill(std::span<BYTE const> bgrx, unsigned stride, Encoder& timing) -> bool {
  Expects(handles.session != nullptr, "encoder session exists");
  Expects(handles.input != nullptr, "encoder input buffer exists");
  using Clock = std::chrono::steady_clock;
  auto                     start = Clock::now();
  NV_ENC_LOCK_INPUT_BUFFER lock  { };
  lock.version     = NV_ENC_LOCK_INPUT_BUFFER_VER;
  lock.inputBuffer = handles.input;
  if (!Check(driver.api.nvEncLockInputBuffer(handles.session, &lock), "lock input")) return false;
  timing.times.upload = Clock::now() - start;
  start               = Clock::now();
  auto status = ConvertInput(lock, { aligned.width, aligned.height }, bgrx, stride);
  timing.times.convert = Clock::now() - start;
  start                = Clock::now();
  auto unlocked = Check(driver.api.nvEncUnlockInputBuffer(handles.session, handles.input), "unlock input");
  timing.times.upload += Clock::now() - start;
  return Check(status, "BT.709 conversion") && unlocked;
}
auto Encoder::Impl::Close() -> void {
  if (handles.input) Check(driver.api.nvEncDestroyInputBuffer(handles.session, handles.input), "destroy input");
  if (handles.output)
    Check(driver.api.nvEncDestroyBitstreamBuffer(handles.session, handles.output), "destroy bitstream");
  if (handles.session) Check(driver.api.nvEncDestroyEncoder(handles.session), "destroy session");
  if (driver.context) Check(driver.cuda->cuDevicePrimaryCtxRelease(driver.device), "release CUDA context");
  handles.input   = nullptr;
  handles.output  = nullptr;
  handles.session = nullptr;
  driver.context  = nullptr;
  nvenc_free_functions(&driver.loader);
  cuda_free_functions(&driver.cuda);
  first = true;
}
Encoder::Encoder() : impl(std::make_unique<Impl>()) { }
Encoder::~Encoder() {
  Close();
}
auto Encoder::Close() -> void {
  impl->Close();
}
auto Encoder::IsOpen() const -> bool {
  return impl->handles.output != nullptr;
}
auto Encoder::Timing() const -> EncodingTimes const& { return times; }
auto Encoder::TooSmall() const -> bool {
  return impl->small;
}
auto Encoder::Error() const -> std::string const& {
  return impl->error;
}
auto Encoder::UnavailableReason() -> std::string {
  static std::string const reason = [] {
    Impl probe;
    auto available = probe.Load();
    auto error     = probe.error;
    probe.Close();
    return available ? std::string{ } : error;
  }();
  return reason;
}
auto Encoder::Available() -> bool {
  return UnavailableReason().empty();
}
auto Encoder::Impl::Capability(NV_ENC_CAPS query, int& value, char const* operation) -> bool {
  NV_ENC_CAPS_PARAM caps{ .version = NV_ENC_CAPS_PARAM_VER, .capsToQuery = query, .reserved = { } };
  return Check(driver.api.nvEncGetEncodeCaps(handles.session, NV_ENC_CODEC_H264_GUID, &caps, &value), operation);
}
auto Encoder::Impl::MinimumSize() -> bool {
  int  min_width  = 0;
  int  min_height = 0;
  bool ok         = Capability(NV_ENC_CAPS_WIDTH_MIN, min_width, "minimum width");
  ok    = Capability(NV_ENC_CAPS_HEIGHT_MIN, min_height, "minimum height") && ok;
  small = ok && (std::cmp_less(aligned.width, min_width) || std::cmp_less(aligned.height, min_height));
  if (small) error = "surface below NVENC minimum picture size";
  return ok;
}
auto Encoder::Open(Extent size, unsigned bitrate, unsigned fps) -> bool {
  auto const [width, height] = size;
  Expects(width > 0, "picture width is positive");
  Expects(height > 0, "picture height is positive");
  Expects(bitrate > 0, "encoder bitrate is positive");
  Expects(fps > 0, "encoder frame rate is positive");
  Close();
  impl->error.clear();
  impl->small   = false;
  impl->picture = size;
  impl->aligned = { .width = Aligned(width), .height = Aligned(height) };
  if (!impl->Load() || !impl->Session() || !impl->MinimumSize() || impl->small || !impl->Initialize(bitrate, fps) ||
      !impl->Buffers()) {
    Close();
    return false;
  }
  return true;
}
auto Encoder::Impl::Picture(bool force_idr) const -> NV_ENC_PIC_PARAMS {
  NV_ENC_PIC_PARAMS pic{ };
  pic.version         = NV_ENC_PIC_PARAMS_VER;
  pic.inputBuffer     = handles.input;
  pic.outputBitstream = handles.output;
  pic.bufferFmt       = NV_ENC_BUFFER_FORMAT_IYUV;
  pic.pictureStruct   = NV_ENC_PIC_STRUCT_FRAME;
  pic.inputWidth      = aligned.width;
  pic.inputHeight     = aligned.height;
  if (force_idr || first) pic.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
  return pic;
}
auto Encoder::Encode(std::span<BYTE const> bgrx, unsigned stride, bool force_idr,
                     std::vector<BYTE>& encoded) -> std::span<BYTE const> {
  Expects(IsOpen(), "encoder is open");
  Expects(stride >= impl->aligned.width * 4, "source stride covers aligned width");
  Expects(bgrx.size() >= (std::size_t(impl->aligned.height - 1) * stride) + (std::size_t(impl->aligned.width) * 4),
          "source covers aligned height");
  times.convert = times.upload = times.encode = { };
  if (!impl->Fill(bgrx, stride, *this)) return { };
  auto pic   = impl->Picture(force_idr);
  auto start = std::chrono::steady_clock::now();
  if (!impl->Check(impl->driver.api.nvEncEncodePicture(impl->handles.session, &pic), "encode picture")) return { };
  NV_ENC_LOCK_BITSTREAM lock{ .version = NV_ENC_LOCK_BITSTREAM_VER, .outputBitstream = impl->handles.output };
  if (!impl->Check(impl->driver.api.nvEncLockBitstream(impl->handles.session, &lock), "lock bitstream")) return { };
  times.encode = std::chrono::steady_clock::now() - start;
  auto const* data = static_cast<BYTE const*>(lock.bitstreamBufferPtr);
  encoded.assign(data, data + lock.bitstreamSizeInBytes);
  if (!impl->Check(impl->driver.api.nvEncUnlockBitstream(impl->handles.session, impl->handles.output),
                   "unlock bitstream"))
    return { };
  impl->first = false;
  Ensures(!encoded.empty(), "one access unit produced synchronously");
  return encoded;
}
} // namespace Backend::Avc
