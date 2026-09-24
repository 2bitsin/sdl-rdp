#include <sdl-rdp/core/picture-geometry.hpp>
#include <sdl-rdp/session/handle.hpp>
#include <sdl-rdp/session/peer.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {
using Backend::Extent;
using Backend::PixelBytes;
constexpr std::uint32_t MaximumRefreshMode  = 3;
constexpr std::uint32_t MinimumPacedRefresh = 10;
constexpr std::uint32_t MaximumPort         = 65535;
constexpr std::uint32_t StereoChannels      = 2;
constexpr std::uint32_t LargestMillihertz   = std::numeric_limits<std::int32_t>::max();
// NVENC takes the rate in bits per second as a uint32_t.
constexpr std::uint32_t LargestAvcKbps = std::numeric_limits<std::uint32_t>::max() / 1000;
auto Dimensions(std::uint32_t width, std::uint32_t height) -> Extent {
  if (!width || width > Backend::MaximumPictureWidth) throw std::runtime_error("Desktop width must be 1..16383.");
  if (!height || height > Backend::MaximumPictureHeight) throw std::runtime_error("Desktop height must be 1..65535.");
  return { .width = width, .height = height };
}
template <std::invocable BodyTy>
auto Guarded(sdlrdp_handle* handle, std::invoke_result_t<BodyTy> failure, BodyTy const& body) noexcept
    -> std::invoke_result_t<BodyTy> {
  return Backend::Contained(failure, body,
                            [handle](std::string_view text) { Backend::SetError(handle, std::string{ text }); });
}
auto Opened(sdlrdp_handle* handle, char const* failure) -> sdlrdp_handle& {
  if (!handle) throw std::runtime_error(failure);
  return *handle;
}
auto ValidateConfiguration(sdlrdp_config const& config) -> void {
  if (config.auth < SDLRDP_AUTH_NONE || config.auth > SDLRDP_AUTH_NLA)
    throw std::runtime_error("Invalid authentication mode.");
  Dimensions(config.width, config.height);
  if (config.avc_bitrate_kbps > LargestAvcKbps) throw std::runtime_error("AVC bitrate exceeds NVENC range");
  if (config.codec < SDLRDP_CODEC_AUTO || config.codec > SDLRDP_CODEC_AVC420)
    throw std::runtime_error("Invalid codec preference.");
  if (config.port > MaximumPort) throw std::runtime_error("Open failed: port exceeds 65535.");
}
auto Inside(sdlrdp_rect area, Extent size) -> bool {
  return area.x >= 0 && area.y >= 0 && area.w > 0 && area.h > 0
         && std::cmp_less_equal(std::int64_t{ area.x } + area.w, size.width)
         && std::cmp_less_equal(std::int64_t{ area.y } + area.h, size.height);
}
auto ValidateDamage(std::span<sdlrdp_rect const> damage, Extent size) -> void {
  if (!std::ranges::all_of(damage, [=](sdlrdp_rect area) { return Inside(area, size); }))
    throw std::runtime_error("Present failed: damage rectangle exceeds framebuffer bounds.");
}
auto Tracing() -> bool {
  auto const* trace = std::getenv("SDL_RDP_TRACE");
  return trace && std::string_view(trace) == "1";
}
auto ValidPointer(std::uint32_t w, std::uint32_t h, std::uint32_t x, std::uint32_t y, void const* argb) -> bool {
  if (w > Backend::LargePointerLimit || h > Backend::LargePointerLimit) return false;
  return !(w || h) || (w && h && argb && x < w && y < h);
}
auto ValidRefresh(std::uint32_t mode, std::uint32_t ceiling) -> bool {
  return mode <= MaximumRefreshMode && ceiling && ceiling <= LargestMillihertz / Backend::MillihertzPerHz
         && (!mode || ceiling >= MinimumPacedRefresh);
}
}
auto sdlrdp_last_error() -> char const* {
  return Backend::ErrorStore::Last();
}
static_assert(std::is_same_v<decltype(sdlrdp_version()), std::uint32_t>,
              "the ABI's unsigned is the std::uint32_t these definitions spell");
auto sdlrdp_version() -> std::uint32_t {
  return SDLRDP_ABI_VERSION;
}
auto sdlrdp_open(sdlrdp_config const* config, sdlrdp_handle** out) -> int {
  auto const opened = [&] {
    if (!out) throw std::runtime_error("Open failed: output handle is null.");
    *out = nullptr;
    if (!config) throw std::runtime_error("Open failed: configuration is null.");
    ValidateConfiguration(*config);
    auto handle = std::make_unique<sdlrdp_handle>(*config, Tracing());
    if (config->wait_for_client) handle->Events().Wait(Backend::Deadline::max());
    *out = handle.release();
    return 0;
  };
  auto const failed = [](std::string_view text) {
    Backend::SetError(nullptr, std::format("sdlrdp_open failed: {}", text));
  };
  return Backend::Contained(-1, opened, failed);
}
auto sdlrdp_close(sdlrdp_handle* handle) -> void {
  std::unique_ptr<sdlrdp_handle> const closed{ handle };
}
auto sdlrdp_port(sdlrdp_handle const* handle) -> std::uint32_t {
  Backend::Expects(handle != nullptr, "backend is open");
  return handle->Port();
}
auto sdlrdp_present(sdlrdp_handle* handle, void const* pixels, int pitch, std::uint32_t width, std::uint32_t height,
                    sdlrdp_rect const* rects, std::uint32_t count) -> int {
  return Guarded(handle, -1, [&] {
    auto const size = Dimensions(width, height);
    if (!handle || !pixels || (!rects && count) || std::cmp_less(pitch, width * PixelBytes))
      throw std::runtime_error("Present failed: invalid handle, pixels, rectangles or pitch.");
    std::span const damage{ rects, count };
    ValidateDamage(damage, size);
    auto const bytes = (Backend::Narrowed<std::size_t>(pitch) * (height - 1)) + (std::size_t{ width } * PixelBytes);
    handle->Presentation().Present({ static_cast<std::uint8_t const*>(pixels), bytes },
                                   Backend::Narrowed<std::uint32_t>(pitch), size, damage);
    return 0;
  });
}
auto sdlrdp_poll(sdlrdp_handle* handle, sdlrdp_event* out, std::uint32_t max) -> std::uint32_t {
  Backend::Expects(handle != nullptr, "backend is open");
  return handle->Events().Poll({ out, max });
}
auto sdlrdp_wait(sdlrdp_handle* handle, int timeout) -> int {
  Backend::Expects(handle != nullptr, "backend is open");
  return Guarded(handle, -1, [&] { return handle->Events().Wait(Backend::AbiDeadline(timeout)); });
}
auto sdlrdp_wakeup(sdlrdp_handle* handle) -> void {
  Backend::Expects(handle != nullptr, "backend is open");
  handle->Events().Wakeup();
}
auto sdlrdp_set_codec(sdlrdp_handle* handle, sdlrdp_codec codec) -> int {
  if (!handle || codec < SDLRDP_CODEC_AUTO || codec > SDLRDP_CODEC_AVC420) {
    Backend::SetError(handle, "Invalid handle or codec preference.");
    return -1;
  }
  handle->Presentation().SetCodec(codec);
  return 0;
}
auto sdlrdp_resize(sdlrdp_handle* handle, std::uint32_t width, std::uint32_t height) -> int {
  return Guarded(handle, -1, [&] {
    auto& opened = Opened(handle, "Invalid handle.");
    opened.Presentation().Resize(Dimensions(width, height));
    return 0;
  });
}
auto sdlrdp_set_aspect(sdlrdp_handle* handle, sdlrdp_aspect aspect) -> int {
  return Guarded(handle, -1, [&] {
    Opened(handle, "Invalid handle.").Presentation().SetAspect(aspect);
    return 0;
  });
}
auto sdlrdp_wait_frame(sdlrdp_handle* handle, int timeout) -> int {
  return Guarded(handle, -1, [&] {
    return Opened(handle, "Invalid handle.").Presentation().WaitFrame(Backend::AbiDeadline(timeout));
  });
}
auto sdlrdp_set_pointer(sdlrdp_handle* handle, std::uint32_t w, std::uint32_t h, std::uint32_t x, std::uint32_t y,
                        void const* argb) -> int {
  return Guarded(handle, -1, [&] {
    if (!handle || !ValidPointer(w, h, x, y, argb))
      throw std::runtime_error("Invalid pointer dimensions, hotspot, pixels or handle.");
    std::span const pixels{ static_cast<std::uint8_t const*>(argb), argb ? std::size_t{ w } * h * PixelBytes : 0 };
    handle->Presentation().SetPointer(Backend::PointerShape{ { .width = w, .height = h }, x, y, pixels });
    return 0;
  });
}
auto sdlrdp_set_clipboard_text(sdlrdp_handle* handle, char const* utf8) -> int {
  return Guarded(handle, -1, [&] {
    if (!handle || !utf8) throw std::runtime_error("Invalid clipboard handle or text.");
    auto const held = handle->Session().Lock();
    std::ignore = handle->Clipboard().Replace(utf8);
    OnCurrent(handle->Session(), [](Backend::Peer& current) { current.Signal(); });
    return 0;
  });
}
auto sdlrdp_get_clipboard_text(sdlrdp_handle* handle) -> char const* {
  return Guarded(handle, static_cast<char const*>(nullptr), [&] {
    auto&      opened = Opened(handle, "Invalid clipboard handle.");
    auto const held   = opened.Session().Lock();
    return opened.Clipboard().Export().c_str();
  });
}
auto sdlrdp_has_clipboard_text(sdlrdp_handle* handle) -> int {
  if (!handle) {
    Backend::SetError(handle, "Invalid clipboard handle.");
    return -1;
  }
  auto const held = handle->Session().Lock();
  return !handle->Clipboard().Text().empty();
}
auto sdlrdp_audio_open(sdlrdp_handle* handle) -> int {
  return Guarded(handle, -1, [&] {
    Opened(handle, "Invalid audio handle.").Audio().Open();
    return 0;
  });
}
auto sdlrdp_audio_rate(sdlrdp_handle* handle) -> std::uint32_t {
  return Guarded(handle, 0U, [&] { return Opened(handle, "Invalid audio handle.").Audio().Rate(); });
}
auto sdlrdp_audio_write(sdlrdp_handle* handle, void const* frames, std::uint32_t count) -> int {
  return Guarded(handle, -1, [&] {
    if (!handle || (!frames && count) || std::cmp_greater(count, std::numeric_limits<int>::max()))
      throw std::runtime_error("Invalid audio handle, frames or count.");
    return handle->Audio().Write({ static_cast<std::int16_t const*>(frames), std::size_t{ count } * StereoChannels });
  });
}
auto sdlrdp_audio_wait(sdlrdp_handle* handle, int timeout) -> int {
  return Guarded(handle, -1,
                 [&] { return Opened(handle, "Invalid audio handle.").Audio().Wait(Backend::AbiDeadline(timeout)); });
}
auto sdlrdp_audio_close(sdlrdp_handle* handle) -> void {
  if (!handle) return;
  std::ignore = Guarded(handle, 0, [&] {
    handle->Audio().Close();
    return 0;
  });
}
auto sdlrdp_set_refresh(sdlrdp_handle* handle, std::uint32_t mode, std::uint32_t ceiling) -> int {
  Backend::Expects(handle != nullptr, "backend is open");
  if (!ValidRefresh(mode, ceiling)) {
    Backend::SetError(handle, "Invalid refresh mode or ceiling.");
    return -1;
  }
  handle->Presentation().SetRefresh(Backend::RefreshMode(mode), ceiling);
  return 0;
}
auto sdlrdp_set_relative_mouse(sdlrdp_handle* handle, int enabled) -> int {
  Backend::Expects(handle != nullptr, "backend is open");
  auto const mode = enabled ? Backend::MouseMode::Relative : Backend::MouseMode::Absolute;
  OnCurrent(handle->Session(), [mode](Backend::Peer& current) { current.Point(mode); });
  return 0;
}
