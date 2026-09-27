#include <sdl-rdp/video/avc/encoder.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/freerdp-facade/yuv420.hpp>
#include <sdl-rdp/picture/geometry.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/stopwatch.hpp>
#include <sdl-rdp/video/avc/encoding.hpp>
#include <sdl-rdp/video/avc/preset.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace sdl_rdp::video::avc::detail::encoder {
namespace {
auto LoaderFailure(std::string& detail, std::string_view format, std::string_view name) -> void;
}
}
// abi: the loader hands its log calls the void* context Loaded gave it, which is the detail string.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): Required by the ffnvcodec loader.
#define FFNV_LOG_FUNC(ctx, msg, ...) \
  sdl_rdp::video::avc::detail::encoder::LoaderFailure(*static_cast<std::string*>(ctx), msg, __VA_ARGS__)
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): Required by the ffnvcodec loader.
#define FFNV_DEBUG_LOG_FUNC(ctx, msg, ...) static_cast<void>(0)
#include <sdl-rdp/freerdp-facade/nvenc.hpp>
#include <cstdint>
#include <ffnvcodec/dynlink_loader.h>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <utility>

namespace sdl_rdp::video::avc::detail::encoder {
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::freerdp_facade::PackedI420;
using sdl_rdp::freerdp_facade::RgbToYuv420;
using sdl_rdp::picture::Aligned;
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Stopwatch;
namespace {
using Reporter = std::optional<std::reference_wrapper<Diagnostics const>>;
// Release-step reports: destructors reach them, where a sink that throws has nowhere further to go.
template <class... ArgsTy>
auto Released(Reporter reporter, int status, std::format_string<ArgsTy const&...> operation,
              ArgsTy const&... args) noexcept -> void {
  if (status == 0 || !reporter) return;
  auto const report = [&] {
    reporter->get().Log(LogLevel::Error, std::format("AVC420 {} failed: {}", std::format(operation, args...), status));
  };
  Contained(report, [](std::string_view) noexcept { });
}
template <auto FREE> struct FreesLibrary {
  auto operator()(auto* functions) const noexcept -> void {
    Expects(functions != nullptr, "unique_ptr releases the library it holds");
    FREE(&functions);
  }
};
class DestroysSession {
public:
  DestroysSession() noexcept                                       = default;
  DestroysSession(PNVENCDESTROYENCODER destroy, Reporter reporter) noexcept
      : _destroy{ destroy }, _reporter{ reporter } {
    Expects(destroy != nullptr, "the session comes with its API table");
  }
  auto operator()(void* session) const noexcept -> void {
    Expects(session != nullptr, "unique_ptr releases the session it holds");
    Expects(_destroy != nullptr, "the session came with its API table");
    Released(_reporter, _destroy(session), "destroy session");
  }

private:
  PNVENCDESTROYENCODER _destroy  = nullptr;
  Reporter             _reporter;
};
using CudaLibrary   = std::unique_ptr<CudaFunctions, FreesLibrary<cuda_free_functions>>;
using NvencLibrary  = std::unique_ptr<NvencFunctions, FreesLibrary<nvenc_free_functions>>;
using EncodeSession = std::unique_ptr<void, DestroysSession>;
auto LoaderFailure(std::string& detail, std::string_view format, std::string_view name) -> void {
  Expects(format == "Cannot load %s\n", "the loader reports one message shape");
  detail = std::format("cannot load {}", name);
}
}
struct Encoder::Impl {
public:
  explicit Impl(Reporter reporter) noexcept;
  auto     Fail(std::string_view operation, std::string_view why)                                 -> void;
  auto     Check(int status, std::string_view operation)                                          -> bool;
  auto     Load()                                                                                 -> bool;
  auto     Session()                                                                              -> bool;
  auto     Initialize(std::uint32_t bitrate, std::uint32_t fps)                                   -> bool;
  auto Parameters(std::uint32_t fps, NV_ENC_CONFIG& config) const -> NV_ENC_INITIALIZE_PARAMS;
  auto     Buffers()                                                                              -> bool;
  auto     Capability(NV_ENC_CAPS query, int& value, std::string_view operation)                  -> bool;
  auto     MinimumSize()                                                                          -> bool;
  auto     Picture(bool force_idr) const                                                          -> NV_ENC_PIC_PARAMS;
  auto     Fill(std::span<std::uint8_t const> bgrx, std::uint32_t stride, EncodingTimes& times)   -> bool;
  auto     Encoded(NV_ENC_PIC_PARAMS& pic, std::vector<std::byte>& encoded, EncodingTimes& times) -> bool;
  auto     Close() noexcept                                                                       -> void;
  template <auto LOAD, class LibraryTy> auto Loaded(LibraryTy& library, std::string_view name) -> bool;

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
  Reporter    reporter;
  Driver      driver;
  Handles     handles;
  Extent      picture;
  Extent      aligned;
  bool        small    = false;
  bool        first    = true;
  std::string error;
};
Encoder::Impl::Impl(Reporter reporter) noexcept : reporter{ reporter } { }
auto Encoder::Impl::Fail(std::string_view operation, std::string_view why) -> void {
  error = std::format("{} failed: {}", operation, why);
  if (reporter) reporter->get().Log(LogLevel::Error, "AVC420 " + error);
}
auto Encoder::Impl::Check(int status, std::string_view operation) -> bool {
  if (status != 0) Fail(operation, std::to_string(status));
  return status == 0;
}
template <auto LOAD, class LibraryTy> auto Encoder::Impl::Loaded(LibraryTy& library, std::string_view name) -> bool {
  typename LibraryTy::pointer loaded = nullptr;
  std::string                 why;
  auto const                  status = LOAD(&loaded, &why);
  library.reset(loaded);
  if (status != 0) Fail(std::format("load {}", name), why.empty() ? std::to_string(status) : why);
  return status == 0;
}
auto Encoder::Impl::Load() -> bool {
  return Loaded<cuda_load_functions>(driver.cuda, "libcuda.so.1")
         && Loaded<nvenc_load_functions>(driver.loader, "libnvidia-encode.so.1")
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
  handles.session = EncodeSession{ session, DestroysSession{ driver.api.nvEncDestroyEncoder, reporter } };
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
auto ConvertInput(NV_ENC_LOCK_INPUT_BUFFER const& lock, Extent size, std::span<std::uint8_t const> bgrx,
                  std::uint32_t stride) -> bool {
  Expects(lock.pitch >= size.width, "I420 pitch covers aligned width");
  // abi: NVENC lends the locked IYUV input as void*.
  std::span const frame{ static_cast<std::uint8_t*>(lock.bufferDataPtr),
                         std::size_t{ lock.pitch } * size.height * 3 / 2 };
  return RgbToYuv420(bgrx, stride, size, PackedI420(frame, lock.pitch, size.height));
}
}
// NVENC lends a buffer from its lock to its unlock: the unlock runs on every path out, and Unlock reports it.
template <class ParametersTy, auto LOCK, auto UNLOCK, auto BUFFER>
class Encoder::Impl::Locked : private Pinned {
public:
  Locked(Impl& owner, ParametersTy lock, std::string_view name)
      : _owner{ owner }, _lock{ lock }, _name{ name }, _locked{ Acquired() } { }
  ~Locked() {
    if (_locked) Released(_owner.reporter, Release(), "unlock {}", _name);
  }
  explicit operator bool() const noexcept {
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
  auto const converted = ConvertInput(input.Lock(), aligned, bgrx, stride);
  times.convert = watch.Lap();
  auto const unlocked = input.Unlock();
  times.upload += watch.Lap();
  if (!converted) Fail("BT.709 conversion", "primitives refused the picture");
  return converted && unlocked;
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
auto Encoder::Impl::Close() noexcept -> void {
  auto* const session = handles.session.get();
  if (handles.input) Released(reporter, driver.api.nvEncDestroyInputBuffer(session, handles.input), "destroy input");
  if (handles.output)
    Released(reporter, driver.api.nvEncDestroyBitstreamBuffer(session, handles.output), "destroy bitstream");
  handles.input  = nullptr;
  handles.output = nullptr;
  handles.session.reset();
  if (driver.context) Released(reporter, driver.cuda->cuDevicePrimaryCtxRelease(driver.device), "release CUDA context");
  driver.context = nullptr;
  driver.loader.reset();
  driver.cuda.reset();
  first = true;
}
Encoder::Encoder(Diagnostics const& diagnostics) : impl(std::make_unique<Impl>(std::cref(diagnostics))) { }
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
    Impl       probe     { std::nullopt };
    auto const available = probe.Load();
    probe.Close();
    return available ? std::string{ } : probe.error;
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
