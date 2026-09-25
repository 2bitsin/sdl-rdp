#include "entry.hpp"

#include <sdl-rdp/backend/exceptions.hpp>
#include <sdl-rdp/peer/peer.hpp>
#include <sdl-rdp/picture/geometry.hpp>
#include <sdl-rdp/session/handle.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
using Backend::Extent;
using Backend::PixelBytes;
using sdl_rdp::backend::DamageOutOfBounds;
using sdl_rdp::backend::Guarded;
using sdl_rdp::backend::InvalidArguments;
using sdl_rdp::backend::InvalidChoice;
using sdl_rdp::backend::OutOfRange;
constexpr std::uint32_t MaximumRefreshMode  = 3;
constexpr std::uint32_t MinimumPacedRefresh = 10;
constexpr std::uint32_t MaximumPort         = 65535;
constexpr std::uint32_t StereoChannels      = 2;
constexpr std::uint32_t LargestMillihertz   = std::numeric_limits<std::int32_t>::max();
// NVENC takes the rate in bits per second as a uint32_t.
constexpr std::uint32_t LargestAvcKbps = std::numeric_limits<std::uint32_t>::max() / 1000;
auto Dimensions(std::uint32_t width, std::uint32_t height) -> Extent {
  if (!width || width > Backend::MaximumPictureWidth)
    throw OutOfRange{ "Desktop width", width, 1, Backend::MaximumPictureWidth };
  if (!height || height > Backend::MaximumPictureHeight)
    throw OutOfRange{ "Desktop height", height, 1, Backend::MaximumPictureHeight };
  return { .width = width, .height = height };
}
auto ValidateConfiguration(sdlrdp_config const& config) -> void {
  if (config.auth < SDLRDP_AUTH_NONE || config.auth > SDLRDP_AUTH_NLA) throw InvalidChoice{ "authentication mode" };
  Dimensions(config.width, config.height);
  if (config.avc_bitrate_kbps > LargestAvcKbps)
    throw OutOfRange{ "Configured AVC bitrate (kbps)", config.avc_bitrate_kbps, 0, LargestAvcKbps };
  if (config.codec < SDLRDP_CODEC_AUTO || config.codec > SDLRDP_CODEC_AVC420) throw InvalidChoice{ "codec preference" };
  if (config.port > MaximumPort) throw OutOfRange{ "Configured port", config.port, 0, MaximumPort };
}
auto Inside(sdlrdp_rect area, Extent size) -> bool {
  return area.x >= 0 && area.y >= 0 && area.w > 0 && area.h > 0
         && std::cmp_less_equal(std::int64_t{ area.x } + area.w, size.width)
         && std::cmp_less_equal(std::int64_t{ area.y } + area.h, size.height);
}
auto ValidateDamage(std::span<sdlrdp_rect const> damage, Extent size) -> void {
  if (!std::ranges::all_of(damage, [=](sdlrdp_rect area) { return Inside(area, size); })) throw DamageOutOfBounds{ };
}
auto Tracing() -> bool {
  auto const* trace = std::getenv("SDL_RDP_TRACE");
  return trace && std::string_view(trace) == "1";
}
auto ValidPointer(std::uint32_t w, std::uint32_t h, std::uint32_t x, std::uint32_t y, bool pixels) -> bool {
  if (w > Backend::LargePointerLimit || h > Backend::LargePointerLimit) return false;
  return !(w || h) || (w && h && pixels && x < w && y < h);
}
auto ValidRefresh(std::uint32_t mode, std::uint32_t ceiling) -> bool {
  return mode <= MaximumRefreshMode && ceiling && ceiling <= LargestMillihertz / Backend::MillihertzPerHz
         && (!mode || ceiling >= MinimumPacedRefresh);
}
}
auto sdlrdp_last_error() -> char const* {
  return Backend::ErrorStore::Last().c_str();
}
static_assert(std::is_same_v<decltype(sdlrdp_version()), std::uint32_t>,
              "the ABI's unsigned is the std::uint32_t these definitions spell");
auto sdlrdp_version() -> std::uint32_t {
  return SDLRDP_ABI_VERSION;
}
auto sdlrdp_open(sdlrdp_config const* config, sdlrdp_handle** out) -> int {
  auto const opened = [&] {
    if (!out) throw Backend::NullArgument{ "Open handle output" };
    *out = nullptr;
    if (!config) throw Backend::NullArgument{ "Open configuration" };
    ValidateConfiguration(*config);
    auto handle = std::make_unique<sdlrdp_handle>(*config, Tracing());
    if (config->wait_for_client) handle->Events().Wait(Backend::Deadline::max());
    *out = handle.release();
    return 0;
  };
  auto const failed = [](std::string_view text) {
    Backend::ErrorStore::PublishDetached(std::format("sdlrdp_open failed: {}", text));
  };
  return Backend::Contained(-1, opened, failed);
}
auto sdlrdp_close(sdlrdp_handle* handle) -> void {
  std::unique_ptr<sdlrdp_handle> const closed{ handle };
}
auto sdlrdp_port(sdlrdp_handle const* handle) -> std::uint32_t {
  Backend::Expects(handle != nullptr, "backend is open");
  auto const& open = *handle;
  return open.Port();
}
auto sdlrdp_present(sdlrdp_handle* handle, void const* pixels, int pitch, std::uint32_t width, std::uint32_t height,
                    sdlrdp_rect const* rects, std::uint32_t count) -> int {
  return Guarded(handle, -1, "Backend handle", [&](sdlrdp_handle& open) {
    auto const size = Dimensions(width, height);
    if (!pixels || (!rects && count) || std::cmp_less(pitch, width * PixelBytes))
      throw InvalidArguments{ "present", "pixels, rectangles or pitch" };
    std::span const damage{ rects, count };
    ValidateDamage(damage, size);
    auto const bytes = (Backend::Narrowed<std::size_t>(pitch) * (height - 1)) + (std::size_t{ width } * PixelBytes);
    open.Presentation().Present({ static_cast<std::uint8_t const*>(pixels), bytes },
                                Backend::Narrowed<std::uint32_t>(pitch), size, damage);
    return 0;
  });
}
auto sdlrdp_poll(sdlrdp_handle* handle, sdlrdp_event* out, std::uint32_t max) -> std::uint32_t {
  return Guarded(handle, 0U, "Backend handle", [&](sdlrdp_handle& open) {
    if (!out && max) throw Backend::NullArgument{ "Poll event output" };
    return open.Events().Poll({ out, max });
  });
}
auto sdlrdp_wait(sdlrdp_handle* handle, int timeout) -> int {
  return Guarded(handle, -1, "Backend handle",
                 [&](sdlrdp_handle& open) { return open.Events().Wait(Backend::AbiDeadline(timeout)); });
}
auto sdlrdp_wakeup(sdlrdp_handle* handle) -> void {
  Backend::Expects(handle != nullptr, "backend is open");
  auto& open = *handle;
  open.Events().Wakeup();
}
auto sdlrdp_set_codec(sdlrdp_handle* handle, sdlrdp_codec codec) -> int {
  return Guarded(handle, -1, "Backend handle", [&](sdlrdp_handle& open) {
    if (codec < SDLRDP_CODEC_AUTO || codec > SDLRDP_CODEC_AVC420) throw InvalidChoice{ "codec preference" };
    open.Presentation().SetCodec(codec);
    return 0;
  });
}
auto sdlrdp_resize(sdlrdp_handle* handle, std::uint32_t width, std::uint32_t height) -> int {
  return Guarded(handle, -1, "Backend handle", [&](sdlrdp_handle& open) {
    open.Presentation().Resize(Dimensions(width, height));
    return 0;
  });
}
auto sdlrdp_set_aspect(sdlrdp_handle* handle, sdlrdp_aspect aspect) -> int {
  return Guarded(handle, -1, "Backend handle", [&](sdlrdp_handle& open) {
    open.Presentation().SetAspect(aspect);
    return 0;
  });
}
auto sdlrdp_wait_frame(sdlrdp_handle* handle, int timeout) -> int {
  return Guarded(handle, -1, "Backend handle",
                 [&](sdlrdp_handle& open) { return open.Presentation().WaitFrame(Backend::AbiDeadline(timeout)); });
}
auto sdlrdp_set_pointer(sdlrdp_handle* handle, std::uint32_t w, std::uint32_t h, std::uint32_t x, std::uint32_t y,
                        void const* argb) -> int {
  return Guarded(handle, -1, "Pointer handle", [&](sdlrdp_handle& open) {
    if (!ValidPointer(w, h, x, y, argb != nullptr))
      throw InvalidArguments{ "pointer", "dimensions, hotspot or pixels" };
    std::span const pixels{ static_cast<std::uint8_t const*>(argb), argb ? std::size_t{ w } * h * PixelBytes : 0 };
    open.Presentation().SetPointer(Backend::PointerShape{ { .width = w, .height = h }, x, y, pixels });
    return 0;
  });
}
auto sdlrdp_set_clipboard_text(sdlrdp_handle* handle, char const* utf8) -> int {
  return Guarded(handle, -1, "Clipboard handle", [&](sdlrdp_handle& open) {
    if (!utf8) throw Backend::NullArgument{ "Clipboard text" };
    auto const held = open.Session().Lock();
    std::ignore = open.Clipboard().Replace(utf8);
    OnCurrent(open.Session(), [](Backend::Peer& current) { current.Signal(); });
    return 0;
  });
}
auto sdlrdp_get_clipboard_text(sdlrdp_handle* handle) -> char const* {
  return Guarded(handle, static_cast<char const*>(nullptr), "Clipboard handle", [](sdlrdp_handle& open) {
    auto const held = open.Session().Lock();
    return open.Clipboard().Export().c_str();
  });
}
auto sdlrdp_has_clipboard_text(sdlrdp_handle* handle) -> int {
  return Guarded(handle, -1, "Clipboard handle", [](sdlrdp_handle& open) {
    auto const held = open.Session().Lock();
    return int{ !open.Clipboard().Text().empty() };
  });
}
auto sdlrdp_audio_open(sdlrdp_handle* handle) -> int {
  return Guarded(handle, -1, "Audio handle", [](sdlrdp_handle& open) {
    open.Audio().Open();
    return 0;
  });
}
auto sdlrdp_audio_rate(sdlrdp_handle* handle) -> std::uint32_t {
  return Guarded(handle, 0U, "Audio handle", [](sdlrdp_handle& open) { return open.Audio().Rate(); });
}
auto sdlrdp_audio_write(sdlrdp_handle* handle, void const* frames, std::uint32_t count) -> int {
  return Guarded(handle, -1, "Audio handle", [&](sdlrdp_handle& open) {
    if ((!frames && count) || std::cmp_greater(count, std::numeric_limits<int>::max()))
      throw InvalidArguments{ "audio write", "frames or count" };
    return open.Audio().Write({ static_cast<std::int16_t const*>(frames), std::size_t{ count } * StereoChannels });
  });
}
auto sdlrdp_audio_wait(sdlrdp_handle* handle, int timeout) -> int {
  return Guarded(handle, -1, "Audio handle",
                 [&](sdlrdp_handle& open) { return open.Audio().Wait(Backend::AbiDeadline(timeout)); });
}
auto sdlrdp_audio_close(sdlrdp_handle* handle) -> void {
  std::ignore = Guarded(handle, 0, "Audio handle", [](sdlrdp_handle& open) {
    open.Audio().Close();
    return 0;
  });
}
auto sdlrdp_set_refresh(sdlrdp_handle* handle, std::uint32_t mode, std::uint32_t ceiling) -> int {
  return Guarded(handle, -1, "Backend handle", [&](sdlrdp_handle& open) {
    if (!ValidRefresh(mode, ceiling)) throw InvalidArguments{ "refresh", "mode or ceiling" };
    open.Presentation().SetRefresh(Backend::RefreshMode(mode), ceiling);
    return 0;
  });
}
auto sdlrdp_set_relative_mouse(sdlrdp_handle* handle, int enabled) -> int {
  return Guarded(handle, -1, "Backend handle", [&](sdlrdp_handle& open) {
    auto const mode = enabled ? Backend::MouseMode::Relative : Backend::MouseMode::Absolute;
    OnCurrent(open.Session(), [mode](Backend::Peer& current) { current.Point(mode); });
    return 0;
  });
}
