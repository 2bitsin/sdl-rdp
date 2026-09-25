#include "framebuffer.hpp"
#include "videodata.hpp"
#include <sdl-rdp/SDL3/rdp/backend/boundary.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
namespace sdl3::rdp::video::detail::framebuffer {
using sdl3::rdp::backend::Boundary;
using sdl3::rdp::backend::Operation;
using sdl3::rdp::backend::Surface;
using sdl_rdp::settings::Settings;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
namespace {
constexpr int FrameAcknowledgementWaitMs = 100;
auto BackendRect(SDL_Rect const& rect) noexcept -> sdlrdp_rect {
  return { rect.x, rect.y, rect.w, rect.h };
}
// SDL returns a framebuffer through format, pixels and pitch output parameters.
auto CreateFramebuffer(SDL_VideoDevice* device, SDL_Window* window, SDL_PixelFormat* format, void** pixels, int* pitch)
    -> bool {
  Expects(device != nullptr, "framebuffer has a device");
  Expects(window != nullptr, "framebuffer has a window");
  Expects(format != nullptr, "framebuffer format output exists");
  Expects(pixels != nullptr, "framebuffer pixels output exists");
  Expects(pitch != nullptr, "framebuffer pitch output exists");
  return Boundary([&] {
    int width { };
    int height{ };
    if (!SDL_GetWindowSizeInPixels(window, &width, &height)) return false;
    Surface surface{ width, height, SDL_PIXELFORMAT_XRGB8888 };
    *format = surface.Get()->format;
    *pixels = surface.Get()->pixels;
    *pitch  = surface.Get()->pitch;
    device->internal->Attach(std::move(surface));
    return true;
  });
}
// SDL supplies a borrowed device, window and counted rectangle buffer.
auto UpdateFramebuffer(SDL_VideoDevice* device, [[maybe_unused]] SDL_Window* unused_window, SDL_Rect const* rects,
                       int count) -> bool {
  Expects(device != nullptr, "frame update has a device");
  Expects(count >= 0, "rectangle count is nonnegative");
  auto&      data        = *device->internal;
  auto const framebuffer = data.Framebuffer();
  if (!framebuffer) return SDL_SetError("Couldn't find RDP surface for window");
  if (count == 0) return true;
  Expects(rects != nullptr, "a counted update has its rectangles");
  auto const damage = std::span(rects, static_cast<std::size_t>(count));
  return Boundary([&] { return framebuffer->get().Present(data.Backend(), damage); });
}
// SDL's framebuffer destruction callback borrows its device and window.
auto DestroyFramebuffer(SDL_VideoDevice* device, [[maybe_unused]] SDL_Window* unused_window) -> void {
  Expects(device != nullptr, "framebuffer destruction has a device");
  device->internal->Detach();
}
}
Framebuffer::Framebuffer(Surface surface) noexcept : _surface{ std::move(surface) } {
  auto const* const owned = _surface.Get();
  Expects(owned != nullptr, "framebuffer owns its surface");
}
auto Framebuffer::Present(Driver const& driver, std::span<SDL_Rect const> rects) -> bool {
  auto const& surface = *_surface.Get();
  auto const  damage  = Damage(rects);
  if (driver.Call<Operation::PRESENT>(surface.pixels, surface.pitch, surface.w, surface.h, damage.data(),
                                      Narrowed<std::uint32_t>(damage.size()))
      != 0)
    return driver.Fail();
  if (!driver.Options().Value<&Settings::vsync>()) return true;
  return driver.Call<Operation::WAIT_FRAME>(FrameAcknowledgementWaitMs) >= 0 || driver.Fail();
}
// The buffer keeps its capacity across presents, so a steady rectangle count allocates only on its first present.
auto Framebuffer::Damage(std::span<SDL_Rect const> rects) -> std::span<sdlrdp_rect const> {
  _damage.clear();
  std::ranges::transform(rects, std::back_inserter(_damage), BackendRect);
  return _damage;
}
auto InitFramebuffer(SDL_VideoDevice& device) -> void {
  device.CreateWindowFramebuffer  = CreateFramebuffer;
  device.UpdateWindowFramebuffer  = UpdateFramebuffer;
  device.DestroyWindowFramebuffer = DestroyFramebuffer;
}
}
