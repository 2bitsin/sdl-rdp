#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/auth/account.hpp>
#include <sdl-rdp/configuration/validation.hpp>
#include <sdl-rdp/diagnostics/error-store.hpp>
#include <sdl-rdp/drive/files.hpp>
#include <sdl-rdp/picture/frame-layout.hpp>
#include <sdl-rdp/session/handle.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/video/pointer/layout.hpp>

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace sdl_rdp::backend::detail::main {
using sdl_rdp::diagnostics::ErrorStore;
using sdl_rdp::drive::DriveFiles;
using sdl_rdp::session::SetError;
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::InvalidArguments;
using sdl_rdp::utilities::NullArgument;
static_assert(std::is_same_v<decltype(sdlrdp_version()), std::uint32_t>,
              "the ABI's unsigned is the std::uint32_t these definitions spell");
static_assert(std::is_same_v<decltype(&sdlrdp_lookup_pair),
                             auto (*)(sdlrdp_config const*, char const*, char const*, std::uint8_t*)->int>,
              "the ABI's unsigned char hash[16] is the std::uint8_t* defined here");
namespace {
template <class BodyTy> using Result = std::invoke_result_t<BodyTy, sdlrdp_handle&>;
template <class ResultTy> auto Refused(ResultTy failure, std::string_view subject) noexcept -> ResultTy {
  auto const publish = [&] {
    ErrorStore::PublishDetached(NullArgument{ subject }.what());
    return failure;
  };
  return Contained(failure, publish, [](std::string_view) noexcept { });
}
template <std::invocable<sdlrdp_handle&> BodyTy>
auto Serviced(sdlrdp_handle& handle, Result<BodyTy> failure, BodyTy const& body) noexcept -> Result<BodyTy> {
  auto const failing = [&handle](std::string_view text) { SetError(handle, std::string{ text }); };
  return Contained(failure, [&] { return body(handle); }, failing);
}
// An sdlrdp_* caller may pass a null handle: refused here once, the body receives the open handle.
template <std::invocable<sdlrdp_handle&> BodyTy>
auto Guarded(sdlrdp_handle* handle, Result<BodyTy> failure, std::string_view subject, BodyTy const& body) noexcept
    -> Result<BodyTy> {
  if (!handle) return Refused(failure, subject);
  return Serviced(*handle, failure, body);
}
// A drive entry point: the handle refused once, the body receives the current peer's drive files.
template <std::invocable<DriveFiles const&> OperationTy>
auto OnDrive(sdlrdp_handle* handle, OperationTy const& operation) noexcept -> int {
  return Guarded(handle, -1, "Drive handle", [&](sdlrdp_handle& open) { return operation(open.Drive()); });
}
// A drive entry point's path is a C string a caller may pass as null: refused here once, the body receives a view.
template <std::invocable<DriveFiles const&, std::string_view> OperationTy>
auto OnPath(sdlrdp_handle* handle, char const* path, OperationTy const& operation) noexcept -> int {
  if (!handle) return Refused(-1, "Drive handle");
  if (!path) return Refused(-1, "Drive path");
  std::string_view const view{ path };
  return Serviced(*handle, -1, [&](sdlrdp_handle& open) { return operation(open.Drive(), view); });
}
// A drive entry point over an open file: the body receives the file, checked against the current peer.
template <std::invocable<sdlrdp_file&> OperationTy>
auto OnFile(sdlrdp_handle* handle, sdlrdp_file* file, OperationTy const& operation) noexcept -> int {
  return OnDrive(handle, [&](DriveFiles const& files) {
    if (!file) throw NullArgument{ "Drive file" };
    return operation(files.Attached(*file));
  });
}
// abi: the caller's transfer buffer, null only when empty.
auto Buffer(void* data, std::size_t size) -> std::optional<std::span<std::byte>> {
  if (!data && size) return std::nullopt;
  return std::span{ static_cast<std::byte*>(data), size };
}
auto Buffer(void const* data, std::size_t size) -> std::optional<std::span<std::byte const>> {
  if (!data && size) return std::nullopt;
  return std::span{ static_cast<std::byte const*>(data), size };
}
// A read fills the caller's bytes, a write sends them: one shape over the constness of the buffer.
template <class ByteTy>
auto Transferred(sdlrdp_handle* handle, sdlrdp_file* file, std::uint64_t offset,
                 std::optional<std::span<ByteTy>> bytes) noexcept -> int {
  return OnFile(handle, file, [&](sdlrdp_file& attached) {
    if (!bytes) throw InvalidArguments{ "drive transfer", "buffer" };
    return attached.Transfer(offset, *bytes);
  });
}
auto Tracing() -> bool {
  auto const* trace = std::getenv("SDL_RDP_TRACE");
  return trace && std::string_view(trace) == "1";
}
}
}

using sdl_rdp::auth::Account;
using sdl_rdp::backend::detail::main::Buffer;
using sdl_rdp::backend::detail::main::Guarded;
using sdl_rdp::backend::detail::main::OnDrive;
using sdl_rdp::backend::detail::main::OnFile;
using sdl_rdp::backend::detail::main::OnPath;
using sdl_rdp::backend::detail::main::Refused;
using sdl_rdp::backend::detail::main::Tracing;
using sdl_rdp::backend::detail::main::Transferred;
using sdl_rdp::configuration::Validate;
using sdl_rdp::diagnostics::ErrorStore;
using sdl_rdp::drive::DriveFiles;
using sdl_rdp::picture::FrameLayout;
using sdl_rdp::session::StereoChannels;
using sdl_rdp::utilities::AbiDeadline;
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::Deadline;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::InvalidArguments;
using sdl_rdp::utilities::NullArgument;
using sdl_rdp::video::pointer::PointerLayout;

auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_last_error() -> char const* {
  return ErrorStore::Last().c_str();
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_version() -> std::uint32_t {
  return SDLRDP_ABI_VERSION;
}
auto _Public_(SDLRDP_ABI_VERSION)
    sdlrdp_verify_pair(sdlrdp_config const* config, char const* domain, char const* user, char const* password) -> int {
  if (!config || !domain || !user || !password) return 0;
  return int{ Account{ *config }.Verifies(domain, user, password) };
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_lookup_pair(sdlrdp_config const* config, char const* domain, char const* user,
                                                     std::uint8_t hash[16]) -> int {
  if (!config || !domain || !user || !hash) return 0;
  auto const looked_up = [&] {
    auto const found = Account{ *config }.NtHash(domain, user);
    if (found) std::ranges::copy(found->Bytes(), hash);
    return int{ found.has_value() };
  };
  return Contained(0, looked_up, [](std::string_view text) { ErrorStore::PublishDetached(std::string{ text }); });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_open(sdlrdp_config const* config, sdlrdp_handle** out) -> int {
  auto const opened = [&] {
    if (!out) throw NullArgument{ "Open handle output" };
    *out = nullptr;
    if (!config) throw NullArgument{ "Open configuration" };
    Validate(*config);
    auto handle = std::make_unique<sdlrdp_handle>(*config, Tracing());
    if (config->wait_for_client) handle->Events().Wait(Deadline::max());
    *out = handle.release();
    return 0;
  };
  auto const failed = [](std::string_view text) {
    ErrorStore::PublishDetached(std::format("sdlrdp_open failed: {}", text));
  };
  return Contained(-1, opened, failed);
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_close(sdlrdp_handle* handle) -> void {
  std::unique_ptr<sdlrdp_handle> const closed{ handle };
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_port(sdlrdp_handle const* handle) -> std::uint32_t {
  Expects(handle != nullptr, "backend is open");
  auto const& open = *handle;
  return open.Port();
}
auto _Public_(SDLRDP_ABI_VERSION)
    sdlrdp_present(sdlrdp_handle* handle, void const* pixels, int pitch, std::uint32_t width, std::uint32_t height,
                   sdlrdp_rect const* rects, std::uint32_t count) -> int {
  return Guarded(handle, -1, "Backend handle", [&](sdlrdp_handle& open) {
    FrameLayout const layout{ width, height, pitch };
    if (!pixels || (!rects && count)) throw InvalidArguments{ "present", "pixels or rectangles" };
    open.Presentation().Present({ static_cast<std::uint8_t const*>(pixels), layout.Bytes() }, layout, { rects, count });
    return 0;
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_poll(sdlrdp_handle* handle, sdlrdp_event* out, std::uint32_t max)
    -> std::uint32_t {
  return Guarded(handle, 0U, "Backend handle", [&](sdlrdp_handle& open) {
    if (!out && max) throw NullArgument{ "Poll event output" };
    return open.Events().Poll({ out, max });
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_wait(sdlrdp_handle* handle, int timeout) -> int {
  return Guarded(handle, -1, "Backend handle",
                 [&](sdlrdp_handle& open) { return open.Events().Wait(AbiDeadline(timeout)); });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_wakeup(sdlrdp_handle* handle) -> void {
  Expects(handle != nullptr, "backend is open");
  auto& open = *handle;
  open.Events().Wakeup();
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_set_codec(sdlrdp_handle* handle, sdlrdp_codec codec) -> int {
  return Guarded(handle, -1, "Backend handle", [&](sdlrdp_handle& open) {
    open.Presentation().SetCodec(codec);
    return 0;
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_resize(sdlrdp_handle* handle, std::uint32_t width, std::uint32_t height)
    -> int {
  return Guarded(handle, -1, "Backend handle", [&](sdlrdp_handle& open) {
    open.Presentation().Resize({ .width = width, .height = height });
    return 0;
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_set_aspect(sdlrdp_handle* handle, sdlrdp_aspect aspect) -> int {
  return Guarded(handle, -1, "Backend handle", [&](sdlrdp_handle& open) {
    open.Presentation().SetAspect(aspect);
    return 0;
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_wait_frame(sdlrdp_handle* handle, int timeout) -> int {
  return Guarded(handle, -1, "Backend handle",
                 [&](sdlrdp_handle& open) { return open.Presentation().WaitFrame(AbiDeadline(timeout)); });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_set_pointer(sdlrdp_handle* handle, std::uint32_t w, std::uint32_t h,
                                                     std::uint32_t x, std::uint32_t y, void const* argb) -> int {
  return Guarded(handle, -1, "Pointer handle", [&](sdlrdp_handle& open) {
    PointerLayout const layout{ { .width = w, .height = h }, x, y };
    if (!argb && layout.Bytes()) throw NullArgument{ "Pointer pixels" };
    open.Presentation().SetPointer(layout, { static_cast<std::uint8_t const*>(argb), argb ? layout.Bytes() : 0 });
    return 0;
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_set_clipboard_text(sdlrdp_handle* handle, char const* utf8) -> int {
  return Guarded(handle, -1, "Clipboard handle", [&](sdlrdp_handle& open) {
    if (!utf8) throw NullArgument{ "Clipboard text" };
    open.SetClipboardText(utf8);
    return 0;
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_get_clipboard_text(sdlrdp_handle* handle) -> char const* {
  return Guarded(handle, static_cast<char const*>(nullptr), "Clipboard handle",
                 [](sdlrdp_handle& open) { return open.ClipboardText().c_str(); });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_has_clipboard_text(sdlrdp_handle* handle) -> int {
  return Guarded(handle, -1, "Clipboard handle", [](sdlrdp_handle& open) { return int{ open.HasClipboardText() }; });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_audio_open(sdlrdp_handle* handle) -> int {
  return Guarded(handle, -1, "Audio handle", [](sdlrdp_handle& open) {
    open.Audio().Open();
    return 0;
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_audio_rate(sdlrdp_handle* handle) -> std::uint32_t {
  return Guarded(handle, 0U, "Audio handle", [](sdlrdp_handle& open) { return open.Audio().Rate(); });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_audio_write(sdlrdp_handle* handle, void const* frames, std::uint32_t count)
    -> int {
  return Guarded(handle, -1, "Audio handle", [&](sdlrdp_handle& open) {
    if ((!frames && count) || std::cmp_greater(count, std::numeric_limits<int>::max()))
      throw InvalidArguments{ "audio write", "frames or count" };
    return open.Audio().Write({ static_cast<std::int16_t const*>(frames), std::size_t{ count } * StereoChannels });
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_audio_wait(sdlrdp_handle* handle, int timeout) -> int {
  return Guarded(handle, -1, "Audio handle",
                 [&](sdlrdp_handle& open) { return open.Audio().Wait(AbiDeadline(timeout)); });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_audio_close(sdlrdp_handle* handle) -> void {
  std::ignore = Guarded(handle, 0, "Audio handle", [](sdlrdp_handle& open) {
    open.Audio().Close();
    return 0;
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_set_refresh(sdlrdp_handle* handle, std::uint32_t mode, std::uint32_t ceiling)
    -> int {
  return Guarded(handle, -1, "Backend handle", [&](sdlrdp_handle& open) {
    open.Presentation().SetRefresh(mode, ceiling);
    return 0;
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_set_relative_mouse(sdlrdp_handle* handle, int enabled) -> int {
  return Guarded(handle, -1, "Backend handle", [&](sdlrdp_handle& open) {
    open.SetRelativeMouse(enabled != 0);
    return 0;
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_drive_list(sdlrdp_handle* handle, sdlrdp_drive* out, std::uint32_t max)
    -> int {
  return OnDrive(handle, [&](DriveFiles const& files) {
    if ((!out && max) || std::cmp_greater(max, std::numeric_limits<int>::max()))
      throw InvalidArguments{ "drive list", "output or count" };
    return files.List({ out, max });
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_drive_open(sdlrdp_handle* handle, std::uint32_t drive, char const* path,
                                                    std::uint32_t flags, sdlrdp_file** out) -> int {
  if (!out) return Refused(-1, "Drive open output");
  *out = nullptr;
  return OnPath(handle, path, [&](DriveFiles const& files, std::string_view name) {
    *out = files.Open(drive, name, flags).release();
    return 0;
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_drive_close(sdlrdp_handle* handle, sdlrdp_file* file) -> int {
  std::unique_ptr<sdlrdp_file> const owned(file);
  return OnFile(handle, file, [](sdlrdp_file& attached) {
    attached.Close();
    return 0;
  });
}
auto _Public_(SDLRDP_ABI_VERSION)
    sdlrdp_drive_read(sdlrdp_handle* h, sdlrdp_file* f, std::uint64_t offset, void* data, std::size_t size) -> int {
  return Transferred(h, f, offset, Buffer(data, size));
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_drive_write(sdlrdp_handle* h, sdlrdp_file* f, std::uint64_t offset,
                                                     void const* data, std::size_t size) -> int {
  return Transferred(h, f, offset, Buffer(data, size));
}
auto _Public_(SDLRDP_ABI_VERSION)
    sdlrdp_drive_stat(sdlrdp_handle* h, std::uint32_t drive, char const* path, sdlrdp_stat* out) -> int {
  return OnPath(h, path, [&](DriveFiles const& files, std::string_view name) {
    if (!out) throw NullArgument{ "Drive stat output" };
    *out = files.Stat(drive, name);
    return 0;
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_drive_enumerate(sdlrdp_handle* h, std::uint32_t drive, char const* path,
                                                         std::uint32_t offset, sdlrdp_dirent* out, std::uint32_t max)
    -> int {
  return OnPath(h, path, [&](DriveFiles const& files, std::string_view name) {
    if (!out && max) throw NullArgument{ "Drive directory output" };
    return files.Enumerate(drive, name, offset, { out, max });
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_drive_mkdir(sdlrdp_handle* h, std::uint32_t drive, char const* path) -> int {
  return OnPath(h, path, [&](DriveFiles const& files, std::string_view name) {
    files.MakeDirectory(drive, name);
    return 0;
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_drive_remove(sdlrdp_handle* h, std::uint32_t drive, char const* path) -> int {
  return OnPath(h, path, [&](DriveFiles const& files, std::string_view name) {
    files.Remove(drive, name);
    return 0;
  });
}
auto _Public_(SDLRDP_ABI_VERSION)
    sdlrdp_drive_rename(sdlrdp_handle* h, std::uint32_t drive, char const* path, char const* destination) -> int {
  return OnPath(h, path, [&](DriveFiles const& files, std::string_view name) {
    if (!destination) throw NullArgument{ "Drive rename destination" };
    files.Rename(drive, name, destination);
    return 0;
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_drive_fstat(sdlrdp_handle* h, sdlrdp_file* file, sdlrdp_stat* out) -> int {
  return OnFile(h, file, [&](sdlrdp_file& attached) {
    if (!out) throw NullArgument{ "Drive fstat output" };
    *out = attached.Stat();
    return 0;
  });
}
auto _Public_(SDLRDP_ABI_VERSION) sdlrdp_drive_flush(sdlrdp_handle* h, sdlrdp_file* file) -> int {
  return OnFile(h, file, [](sdlrdp_file&) {
    // FreeRDP 3.32 drive_main.c:754 has no FLUSH_BUFFERS case; synchronous writes are already acknowledged.
    return 0;
  });
}
