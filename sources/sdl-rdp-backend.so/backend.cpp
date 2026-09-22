#include "_detail/state.hpp"
#include <algorithm>
#include <string>
#include <format>
#include <stdexcept>
#include <span>

namespace {
thread_local std::string last_error;
// A 32-bit row must fit BitmapUpdate bitmapLength (UINT16); height is UINT16.
constexpr unsigned MAX_WIDTH = 65535 / 4, MAX_HEIGHT = 65535;
void Dimensions(unsigned width, unsigned height)
{
  if (!width || width > MAX_WIDTH) throw std::runtime_error("Desktop width must be 1..16383.");
  if (!height || height > MAX_HEIGHT) throw std::runtime_error("Desktop height must be 1..65535.");
}
}
const char* sdlrdp_last_error() { return last_error.c_str(); }

unsigned sdlrdp_version() { return SDLRDP_ABI_VERSION; }
int sdlrdp_open(sdlrdp_config const* config, sdlrdp_handle** out)
{
  try {
    if (!out) throw std::runtime_error("Open failed: output handle is null.");
    *out = nullptr;
    if (!config) throw std::runtime_error("Open failed: configuration is null.");
    Dimensions(config->width, config->height);
    if (config->codec < SDLRDP_CODEC_AUTO || config->codec > SDLRDP_CODEC_RAW)
      throw std::runtime_error("Invalid codec preference.");
    if (config->port > 65535) throw std::runtime_error("Open failed: port exceeds 65535.");
    auto handle = std::make_unique<sdlrdp_handle>();
    handle->state = std::make_unique<Backend::State>(*config);
    if (config->wait_for_client) handle->state->Wait(-1);
    *out = handle.release();
    return 0;
  } catch (std::exception const& error) {
    last_error = std::format("sdlrdp_open failed: {}", error.what());
    return -1;
  }
}
void sdlrdp_close(sdlrdp_handle* handle) { delete handle; }
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
      return area.x >= 0 && area.y >= 0 && area.w > 0 && area.h > 0
        && unsigned(area.x) <= width && unsigned(area.w) <= width - unsigned(area.x)
        && unsigned(area.y) <= height && unsigned(area.h) <= height - unsigned(area.y);
    };
    std::span damage{rects, count};
    if (!std::ranges::all_of(damage, valid))
      throw std::runtime_error("Present failed: damage rectangle exceeds framebuffer bounds.");
    handle->state->Present(pixels, pitch, width, height, damage); return 0;
  } catch (std::exception const& error) { last_error = error.what(); return -1; }
}
unsigned sdlrdp_poll(sdlrdp_handle* handle, sdlrdp_event* out, unsigned max)
{
  Backend::Expects(handle != nullptr, "backend is open");
  return handle->state->Poll(out, max);
}
int sdlrdp_wait(sdlrdp_handle* handle, int timeout)
{
  Backend::Expects(handle != nullptr, "backend is open");
  try { return handle->state->Wait(timeout); }
  catch (std::exception const& error) { last_error = error.what(); return -1; }
}
void sdlrdp_wakeup(sdlrdp_handle* handle)
{
  Backend::Expects(handle != nullptr, "backend is open");
  handle->state->Wakeup();
}

int sdlrdp_set_codec(sdlrdp_handle* handle, sdlrdp_codec codec)
{
  if (!handle || codec < SDLRDP_CODEC_AUTO || codec > SDLRDP_CODEC_RAW) {
    last_error = "Invalid handle or codec preference.";
    return -1;
  }
  handle->state->codec.store(codec);
  return 0;
}
