#include "_detail/state.hpp"
#include <algorithm>
#include <cstdlib>
#include <string>
#include <format>
#include <stdexcept>
#include <span>

namespace {
// A 32-bit row must fit BitmapUpdate bitmapLength (UINT16); height is UINT16.
constexpr unsigned MAX_WIDTH = 65535 / 4, MAX_HEIGHT = 65535;
void               Dimensions(unsigned width, unsigned height)
{
  if (!width || width > MAX_WIDTH) throw std::runtime_error("Desktop width must be 1..16383.");
  if (!height || height > MAX_HEIGHT) throw std::runtime_error("Desktop height must be 1..65535.");
}
}
namespace Backend {
void SetError(sdlrdp_handle* handle, std::string text)
{
  ErrorStore::Publish(handle ? &handle->errors : nullptr, std::move(text));
}
}
const char* sdlrdp_last_error() { return Backend::ErrorStore::Last(); }

unsigned sdlrdp_version() { return SDLRDP_ABI_VERSION; }
int      sdlrdp_open(sdlrdp_config const* config, sdlrdp_handle** out)
{
  try {
    if (!out) throw std::runtime_error("Open failed: output handle is null.");
    *out = nullptr;
    if (!config) throw std::runtime_error("Open failed: configuration is null.");
    if (config->auth < SDLRDP_AUTH_NONE || config->auth > SDLRDP_AUTH_NLA)
      throw std::runtime_error("Invalid authentication mode.");
    Dimensions(config->width, config->height);
    if (config->avc_bitrate_kbps > UINT32_MAX / 1000) throw std::runtime_error("AVC bitrate exceeds NVENC range");
    if (config->codec < SDLRDP_CODEC_AUTO || config->codec > SDLRDP_CODEC_AVC420)
      throw std::runtime_error("Invalid codec preference.");
    if (config->port > 65535) throw std::runtime_error("Open failed: port exceeds 65535.");
    auto  handle = std::make_unique<sdlrdp_handle>();
    auto* trace  = std::getenv("SDL_RDP_TRACE");
    handle->state = std::make_unique<Backend::State>(*config, trace && std::string_view(trace) == "1");
    if (config->wait_for_client) handle->state->Wait(-1);
    *out = handle.release();
    return 0;
  } catch (std::exception const& error) {
    Backend::SetError(nullptr, std::format("sdlrdp_open failed: {}", error.what()));
    return -1;
  }
}
void     sdlrdp_close(sdlrdp_handle* handle) { delete handle; }
unsigned sdlrdp_port(sdlrdp_handle const* handle)
{
  Backend::Expects(handle != nullptr, "backend is open");
  return handle->state->port;
}
int sdlrdp_present(sdlrdp_handle* handle, void const* pixels, int pitch,
                   unsigned width, unsigned height, sdlrdp_rect const* rects, unsigned count)
{
  try {
    Dimensions(width, height);
    if (!handle || !pixels || (!rects && count) || pitch < int(width * 4))
      throw std::runtime_error("Present failed: invalid handle, pixels, rectangles or pitch.");
    auto valid = [=](sdlrdp_rect area) {
      return area.x >= 0 && area.y >= 0 && area.w > 0 && area.h > 0 && unsigned(area.x) <= width && unsigned(area.w) <= width - unsigned(area.x) && unsigned(area.y) <= height && unsigned(area.h) <= height - unsigned(area.y);
    };
    std::span const damage{ rects, count };
    if (!std::ranges::all_of(damage, valid))
      throw std::runtime_error("Present failed: damage rectangle exceeds framebuffer bounds.");
    handle->state->trace.Line("present", [&] { return std::format("dirty={}", count); });
    handle->state->Present(pixels, pitch, width, height, damage);
    return 0;
  } catch (std::exception const& error) {
    Backend::SetError(handle, error.what());
    return -1;
  }
}
unsigned sdlrdp_poll(sdlrdp_handle* handle, sdlrdp_event* out, unsigned max)
{
  Backend::Expects(handle != nullptr, "backend is open");
  return handle->state->Poll(out, max);
}
int sdlrdp_wait(sdlrdp_handle* handle, int timeout)
{
  Backend::Expects(handle != nullptr, "backend is open");
  try {
    return handle->state->Wait(timeout);
  } catch (std::exception const& error) {
    Backend::SetError(handle, error.what());
    return -1;
  }
}
void sdlrdp_wakeup(sdlrdp_handle* handle)
{
  Backend::Expects(handle != nullptr, "backend is open");
  handle->state->Wakeup();
}

int sdlrdp_set_codec(sdlrdp_handle* handle, sdlrdp_codec codec)
{
  if (!handle || codec < SDLRDP_CODEC_AUTO || codec > SDLRDP_CODEC_AVC420) {
    Backend::SetError(handle, "Invalid handle or codec preference.");
    return -1;
  }
  handle->state->codec.store(codec);
  return 0;
}
int sdlrdp_resize(sdlrdp_handle* handle, unsigned width, unsigned height)
{
  try {
    if (!handle) throw std::runtime_error("Invalid handle.");
    Dimensions(width, height);
    handle->state->Resize(width, height);
    return 0;
  } catch (std::exception const& error) {
    Backend::SetError(handle, error.what());
    return -1;
  }
}
int sdlrdp_set_aspect(sdlrdp_handle* handle, sdlrdp_aspect aspect)
{
  try {
    if (!handle) throw std::runtime_error("Invalid handle.");
    handle->state->SetAspect(aspect);
    return 0;
  } catch (std::exception const& error) {
    Backend::SetError(handle, error.what());
    return -1;
  }
}
int sdlrdp_wait_frame(sdlrdp_handle* handle, int timeout)
{
  try {
    if (!handle) throw std::runtime_error("Invalid handle.");
    return handle->state->WaitFrame(timeout);
  } catch (std::exception const& error) {
    Backend::SetError(handle, error.what());
    return -1;
  }
}

int sdlrdp_set_pointer(sdlrdp_handle* handle, unsigned w, unsigned h, unsigned x, unsigned y, void const* argb)
{
  try {
    if (!handle || w > 384 || h > 384 || ((w || h) && (!w || !h || !argb || x >= w || y >= h)))
      throw std::runtime_error("Invalid pointer dimensions, hotspot, pixels or handle.");
    handle->state->SetPointer(w, h, x, y, argb);
    return 0;
  } catch (std::exception const& error) {
    Backend::SetError(handle, error.what());
    return -1;
  }
}

int sdlrdp_set_clipboard_text(sdlrdp_handle* handle, char const* text)
{
  try {
    if (!handle || !text) throw std::runtime_error("Invalid clipboard handle or text.");
    std::string            copied(text);
    auto  unicode = Backend::ClipboardUnicode(copied);
    auto& state   = *handle->state;
    std::scoped_lock const lock(state.session_guard);
    state.clipboard.text    = std::move(copied);
    state.clipboard.unicode = std::move(unicode);
    ++state.clipboard.generation;
    if (state.current) state.current->wake.Transition(Backend::WakeEvent::Phase::Pending);
    return 0;
  } catch (std::exception const& error) {
    Backend::SetError(handle, error.what());
    return -1;
  }
}
const char* sdlrdp_get_clipboard_text(sdlrdp_handle* handle)
{
  try {
    if (!handle) throw std::runtime_error("Invalid clipboard handle.");
    auto& state = *handle->state;
    std::scoped_lock const lock(state.session_guard);
    state.clipboard.exported = state.clipboard.text;
    return state.clipboard.exported.c_str();
  } catch (std::exception const& error) {
    Backend::SetError(handle, error.what());
    return nullptr;
  }
}
int sdlrdp_has_clipboard_text(sdlrdp_handle* handle)
{
  if (!handle) {
    Backend::SetError(handle, "Invalid clipboard handle.");
    return -1;
  }
  auto& state = *handle->state;
  std::scoped_lock const lock(state.session_guard);
  return !state.clipboard.text.empty();
}
int sdlrdp_audio_open(sdlrdp_handle* handle)
{
  try {
    if (!handle) throw std::runtime_error("Invalid audio handle.");
    handle->state->OpenAudio();
    return 0;
  } catch (std::exception const& error) {
    Backend::SetError(handle, error.what());
    return -1;
  }
}
unsigned sdlrdp_audio_rate(sdlrdp_handle* handle)
{
  try {
    if (!handle) throw std::runtime_error("Invalid audio handle.");
    return handle->state->AudioRate();
  } catch (std::exception const& error) {
    Backend::SetError(handle, error.what());
    return 0;
  }
}
int sdlrdp_audio_write(sdlrdp_handle* handle, const void* frames, unsigned count)
{
  try {
    if (!handle || (!frames && count) || count > unsigned(INT_MAX))
      throw std::runtime_error("Invalid audio handle, frames or count.");
    return handle->state->WriteAudio(frames, count);
  } catch (std::exception const& error) {
    Backend::SetError(handle, error.what());
    return -1;
  }
}
int sdlrdp_audio_wait(sdlrdp_handle* handle, int timeout)
{
  try {
    if (!handle) throw std::runtime_error("Invalid audio handle.");
    return handle->state->WaitAudio(timeout);
  } catch (std::exception const& error) {
    Backend::SetError(handle, error.what());
    return -1;
  }
}
void sdlrdp_audio_close(sdlrdp_handle* handle)
{
  if (!handle) return;
  try {
    handle->state->CloseAudio();
  } catch (std::exception const& error) {
    Backend::SetError(handle, error.what());
  }
}

int sdlrdp_set_refresh(sdlrdp_handle* handle, unsigned mode, unsigned ceiling)
{
  Backend::Expects(handle != nullptr, "backend is open");
  if (mode > 3 || !ceiling || ceiling > unsigned(INT32_MAX) / 1000 || (mode && ceiling < 10)) {
    last_error = "Invalid refresh mode or ceiling.";
    return -1;
  }
  handle->state->SetRefresh(Backend::RefreshMode(mode), ceiling);
  return 0;
}
