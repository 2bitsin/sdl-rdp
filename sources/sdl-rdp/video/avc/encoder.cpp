#include <sdl-rdp/video/avc/encoder.hpp>
#include <sdl-rdp/picture/geometry.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/stopwatch.hpp>
#include <sdl-rdp/video/avc/encoding.hpp>
#include <sdl-rdp/video/avc/preset.hpp>

#include <winpr/wlog.h>
#include <array>
#include <cstddef>
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): Required by the ffnvcodec loader.
#define FFNV_LOG_FUNC(ctx, msg, ...) WLog_ERR("sdlrdp.avc", msg, __VA_ARGS__)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): Required by the ffnvcodec loader.
#define FFNV_DEBUG_LOG_FUNC(ctx, msg, ...) static_cast<void>(0)
#include <freerdp/primitives.h>
#include <sdl-rdp/freerdp-facade/nvenc.hpp>
#include <cstdint>
#include <ffnvcodec/dynlink_loader.h>
#include <format>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace sdl_rdp::video::avc::detail::encoder {
using sdl_rdp::picture::Aligned;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Stopwatch;
namespace {
template <auto FREE> struct FreesLibrary {
  auto operator()(auto* functions) const noexcept -> void {
    Expects(functions != nullptr, "unique_ptr releases the library it holds");
    FREE(&functions);
  }
};
class DestroysSession {
public:
           DestroysSession() noexcept                    = default;
  explicit DestroysSession(PNVENCDESTROYENCODER destroy) noexcept : _destroy{ destroy } {
    Expects(destroy != nullptr, "the session comes with its API table");
  }
  auto operator()(void* session) const noexcept -> void {
    Expects(session != nullptr, "unique_ptr releases the session it holds");
    Expects(_destroy != nullptr, "the session came with its API table");
    if (auto const status = _destroy(session); status != NV_ENC_SUCCESS)
      WLog_ERR("sdlrdp.avc", "AVC420 destroy session failed: %d", int{ status });
  }

private:
  PNVENCDESTROYENCODER _destroy = nullptr;
};
using CudaLibrary   = std::unique_ptr<CudaFunctions, FreesLibrary<cuda_free_functions>>;
using NvencLibrary  = std::unique_ptr<NvencFunctions, FreesLibrary<nvenc_free_functions>>;
using EncodeSession = std::unique_ptr<void, DestroysSession>;
template <class LibraryTy, class LoadTy> auto Loaded(LibraryTy& library, LoadTy load) -> int {
  typename LibraryTy::pointer loaded = nullptr;
  auto const                  status = load(&loaded, nullptr);
  library.reset(loaded);
  return status;
}
}
struct Encoder::Impl {
public:
  auto Check(int status, std::string_view operation)                                        -> bool;
  auto Load()                                                                               -> bool;
  auto Session()                                                                            -> bool;
  auto Initialize(std::uint32_t bitrate, std::uint32_t fps)                                 -> bool;
  auto Parameters(std::uint32_t fps, NV_ENC_CONFIG& config) const                           -> NV_ENC_INITIALIZE_PARAMS;
  auto Buffers()                                                                            -> bool;
  auto Capability(NV_ENC_CAPS query, int& value, std::string_view operation)                -> bool;
  auto MinimumSize()                                                                        -> bool;
  auto Picture(bool force_idr) const                                                        -> NV_ENC_PIC_PARAMS;
  auto Fill(std::span<std::uint8_t const> bgrx, std::uint32_t stride, EncodingTimes& times) -> bool;
  auto Encoded(NV_ENC_PIC_PARAMS& pic, std::vector<std::byte>& encoded, EncodingTimes& times) -> bool;
  auto Close()                                                                              -> void;

private:
  friend class Encoder;
  template <class ParametersTy, auto LOCK, auto UNLOCK, auto BUFFER> class Locked;
  using Api = NV_ENCODE_API_FUNCTION_LIST;
  using LockedInput = Locked<NV_ENC_LOCK_INPUT_BUFFER, &Api::nvEncLockInputBuffer, &Api::nvEncUnlockInputBuffer,
                             &NV_ENC_LOCK_INPUT_BUFFER::inputBuffer>;
  using LockedBitstream = Locked<NV_ENC_LOCK_BITSTREAM, &Api::nvEncLockBitstream, &Api::nvEncUnlockBitstream,
                                 &NV_ENC_LOCK_BITSTREAM::outputBitstream>;
  struct Driver {
    CudaLibrary                 cuda;
    NvencLibrary                loader;
    NV_ENCODE_API_FUNCTION_LIST api     { };
    CUdevice                    device  = 0;
    CUcontext                   context = nullptr;
  };
  struct Handles {
    EncodeSession     session;
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
auto Encoder::Impl::Check(int status, std::string_view operation) -> bool {
  if (!status) return true;
  error = std::format("AVC420 {} failed: {}", operation, status);
  WLog_ERR("sdlrdp.avc", "%s", error.c_str());
  return false;
}
auto Encoder::Impl::Load() -> bool {
  return Check(Loaded(driver.cuda, cuda_load_functions), "load libcuda.so.1")
         && Check(Loaded(driver.loader, nvenc_load_functions), "load libnvidia-encode.so.1")
         && Check(driver.cuda->cuInit(0), "cuInit");
}
auto Encoder::Impl::Session() -> bool {
  Expects(driver.cuda != nullptr, "CUDA library is loaded");
  Expects(driver.loader != nullptr, "NVENC library is loaded");
  Expects(handles.session == nullptr, "encoder session is fresh");
  Expects(driver.context == nullptr, "CUDA context is fresh");
  driver.api.version = NV_ENCODE_API_FUNCTION_LIST_VER;
  if (!Check(driver.loader->NvEncodeAPICreateInstance(&driver.api), "create API")
      || !Check(driver.cuda->cuDeviceGet(&driver.device, 0), "cuDeviceGet")
      || !Check(driver.cuda->cuDevicePrimaryCtxRetain(&driver.context, driver.device), "retain CUDA context"))
    return false;
  NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS open{ };
  open.version    = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
  open.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
  open.device     = driver.context;
  open.apiVersion = NVENCAPI_VERSION;
  void*      session = nullptr;
  auto const status  = driver.api.nvEncOpenEncodeSessionEx(&open, &session);
  handles.session = EncodeSession{ session, DestroysSession{ driver.api.nvEncDestroyEncoder } };
  return Check(status, "open session");
}
auto Encoder::Impl::Parameters(std::uint32_t fps, NV_ENC_CONFIG& config) const -> NV_ENC_INITIALIZE_PARAMS {
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
  init.encodeConfig      = &config;
  return init;
}
auto Encoder::Impl::Initialize(std::uint32_t bitrate, std::uint32_t fps) -> bool {
  Expects(handles.session != nullptr, "encoder session exists");
  Expects(bitrate > 0, "encoder bitrate is positive");
  Expects(fps > 0, "encoder frame rate is positive");
  NV_ENC_PRESET_CONFIG preset{ };
  preset.version           = NV_ENC_PRESET_CONFIG_VER;
  preset.presetCfg.version = NV_ENC_CONFIG_VER;
  if (!Check(driver.api.nvEncGetEncodePresetConfigEx(handles.session.get(), NV_ENC_CODEC_H264_GUID,
                                                     NV_ENC_PRESET_P4_GUID, NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY,
                                                     &preset),
             "preset"))
    return false;
  auto& config = preset.presetCfg;
  ConfigurePreset(config, bitrate, fps);
  auto init = Parameters(fps, config);
  return Check(driver.api.nvEncInitializeEncoder(handles.session.get(), &init), "initialize encoder");
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
  if (!Check(driver.api.nvEncCreateInputBuffer(handles.session.get(), &in), "create input buffer")) return false;
  handles.input = in.inputBuffer;
  NV_ENC_CREATE_BITSTREAM_BUFFER out{ };
  out.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;
  if (!Check(driver.api.nvEncCreateBitstreamBuffer(handles.session.get(), &out), "create bitstream buffer"))
    return false;
  handles.output = out.bitstreamBuffer;
  return true;
}
namespace {
// abi: NVENC lends the access unit as void* plus its length.
auto Bytes(NV_ENC_LOCK_BITSTREAM const& lock) -> std::span<std::byte const> {
  return { static_cast<std::byte const*>(lock.bitstreamBufferPtr), lock.bitstreamSizeInBytes };
}
auto ConvertInput(NV_ENC_LOCK_INPUT_BUFFER const& lock, prim_size_t const& size, std::span<std::uint8_t const> bgrx,
                  std::uint32_t stride) -> int {
  Expects(lock.pitch >= size.width, "I420 pitch covers aligned width");
  Expects(lock.pitch % 2 == 0, "I420 pitch is even");
  auto*                        y      { static_cast<std::uint8_t*>(lock.bufferDataPtr) };
  std::array<std::uint8_t*, 3> planes { y, y + (std::size_t{ lock.pitch } * size.height),
                                        y + (std::size_t{ lock.pitch } * size.height * 5 / 4) };
  std::array<std::uint32_t, 3> pitches{ lock.pitch, lock.pitch / 2, lock.pitch / 2     };
  return primitives_get()->RGBToYUV420_8u_P3AC4R(bgrx.data(), PIXEL_FORMAT_BGRX32, stride, planes.data(),
                                                 pitches.data(), &size);
}
}
// NVENC lends a buffer from its lock to its unlock: the unlock runs on every path out, and Unlock reports it.
template <class ParametersTy, auto LOCK, auto UNLOCK, auto BUFFER>
class Encoder::Impl::Locked {
public:
  Locked(Impl& owner, ParametersTy lock, std::string_view name)
      : _owner{ owner }, _lock{ lock }, _name{ name }, _locked{ Acquired() } { }
  Locked(Locked const&) = delete;
  Locked(Locked&&)      = delete;
  ~Locked() {
    if (!_locked) return;
    if (auto const status = Release(); status != NV_ENC_SUCCESS)
      WLog_ERR("sdlrdp.avc", "AVC420 unlock %s failed: %d", _name.c_str(), int{ status });
  }
  auto     operator=(Locked const&) -> Locked& = delete;
  auto     operator=(Locked&&)      -> Locked& = delete;
  explicit operator bool() const               noexcept {
    return _locked;
  }
  auto Lock() const -> ParametersTy const& {
    Expects(_locked, "a locked buffer is read");
    return _lock;
  }
  auto Unlock() -> bool {
    Expects(_locked, "a buffer unlocks once");
    return _owner.Check(Release(), std::format("unlock {}", _name));
  }

private:
  auto Acquired() -> bool {
    return _owner.Check((_owner.driver.api.*LOCK)(_owner.handles.session.get(), &_lock), std::format("lock {}", _name));
  }
  auto Release() noexcept -> NVENCSTATUS {
    _locked = false;
    return (_owner.driver.api.*UNLOCK)(_owner.handles.session.get(), _lock.*BUFFER);
  }
  Impl&        _owner;
  ParametersTy _lock;
  std::string  _name;
  bool         _locked;
};
auto Encoder::Impl::Fill(std::span<std::uint8_t const> bgrx, std::uint32_t stride, EncodingTimes& times) -> bool {
  Expects(handles.session != nullptr, "encoder session exists");
  Expects(handles.input != nullptr, "encoder input buffer exists");
  Stopwatch   watch;
  LockedInput input{ *this, { .version = NV_ENC_LOCK_INPUT_BUFFER_VER, .inputBuffer = handles.input }, "input" };
  if (!input) return false;
  times.upload = watch.Lap();
  auto const status = ConvertInput(input.Lock(), { aligned.width, aligned.height }, bgrx, stride);
  times.convert = watch.Lap();
  auto const unlocked = input.Unlock();
  times.upload += watch.Lap();
  return Check(status, "BT.709 conversion") && unlocked;
}
auto Encoder::Impl::Encoded(NV_ENC_PIC_PARAMS& pic, std::vector<std::byte>& encoded, EncodingTimes& times) -> bool {
  Stopwatch const watch;
  if (!Check(driver.api.nvEncEncodePicture(handles.session.get(), &pic), "encode picture")) return false;
  LockedBitstream bitstream{ *this,
                             { .version = NV_ENC_LOCK_BITSTREAM_VER, .outputBitstream = handles.output },
                             "bitstream" };
  if (!bitstream) return false;
  times.encode = watch.Elapsed();
  auto const bytes = Bytes(bitstream.Lock());
  encoded.assign(bytes.begin(), bytes.end());
  return bitstream.Unlock();
}
auto Encoder::Impl::Close() -> void {
  if (handles.input) Check(driver.api.nvEncDestroyInputBuffer(handles.session.get(), handles.input), "destroy input");
  if (handles.output)
    Check(driver.api.nvEncDestroyBitstreamBuffer(handles.session.get(), handles.output), "destroy bitstream");
  handles.input  = nullptr;
  handles.output = nullptr;
  handles.session.reset();
  if (driver.context) Check(driver.cuda->cuDevicePrimaryCtxRelease(driver.device), "release CUDA context");
  driver.context = nullptr;
  driver.loader.reset();
  driver.cuda.reset();
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
auto Encoder::Timing() const -> EncodingTimes const& {
  return times;
}
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
auto Encoder::Impl::Capability(NV_ENC_CAPS query, int& value, std::string_view operation) -> bool {
  NV_ENC_CAPS_PARAM caps{ .version = NV_ENC_CAPS_PARAM_VER, .capsToQuery = query, .reserved = { } };
  return Check(driver.api.nvEncGetEncodeCaps(handles.session.get(), NV_ENC_CODEC_H264_GUID, &caps, &value), operation);
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
auto Encoder::Open(Extent size, std::uint32_t bitrate, std::uint32_t fps) -> bool {
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
  if (!impl->Load() || !impl->Session() || !impl->MinimumSize() || impl->small || !impl->Initialize(bitrate, fps)
      || !impl->Buffers()) {
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
auto Encoder::Encode(std::span<std::uint8_t const> bgrx, std::uint32_t stride, bool force_idr,
                     std::vector<std::byte>& encoded) -> std::span<std::byte const> {
  Expects(IsOpen(), "encoder is open");
  Expects(stride >= impl->aligned.width * 4, "source stride covers aligned width");
  Expects(bgrx.size() >= (std::size_t{ impl->aligned.height - 1 } * stride) + (std::size_t{ impl->aligned.width } * 4),
          "source covers aligned height");
  times.convert = times.upload = times.encode = { };
  if (!impl->Fill(bgrx, stride, times)) return { };
  auto pic = impl->Picture(force_idr);
  if (!impl->Encoded(pic, encoded, times)) return { };
  impl->first = false;
  Ensures(!encoded.empty(), "one access unit produced synchronously");
  return encoded;
}
}
