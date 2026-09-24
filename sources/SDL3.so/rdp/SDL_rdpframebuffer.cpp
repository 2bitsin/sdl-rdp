#include "SDL_rdpvideo.hpp"
#include "boundary.hpp"
#include <ranges>
namespace rdp {
namespace {
constexpr int FrameAcknowledgementWaitMs = 100;
auto Damage(std::span<SDL_Rect const> rects) -> std::vector<sdlrdp_rect> {
  return rects | std::views::transform([](SDL_Rect const& rect) { return sdlrdp_rect{rect.x, rect.y, rect.w, rect.h}; })
      | std::ranges::to<std::vector>();
}
auto Present(Driver const& driver, SDL_Surface const& surface, std::span<sdlrdp_rect const> damage) -> bool {
  if (driver.Call<Operation::PRESENT>(surface.pixels, surface.pitch, surface.w, surface.h, damage.data(),
                                      static_cast<unsigned>(damage.size())) != 0) return driver.Fail();
  if (!driver.Options().Boolean(SDL_HINT_RDP_VSYNC, false)) return true;
  return driver.Call<Operation::WAIT_FRAME>(FrameAcknowledgementWaitMs) >= 0 || driver.Fail();
}
// SDL returns a framebuffer through format, pixels and pitch output parameters.
auto CreateFramebuffer(SDL_VideoDevice* device, SDL_Window* window, SDL_PixelFormat* format, void** pixels,
                       int* pitch) -> bool {
  utilities::Expects(device != nullptr, "framebuffer has a device");
  utilities::Expects(window != nullptr, "framebuffer has a window");
  utilities::Expects(format != nullptr, "framebuffer format output exists");
  utilities::Expects(pixels != nullptr, "framebuffer pixels output exists");
  utilities::Expects(pitch != nullptr, "framebuffer pitch output exists");
  return Boundary([&] {
    int width { };
    int height{ };
    if (!SDL_GetWindowSizeInPixels(window, &width, &height)) return false;
    Surface surface{ width, height, SDL_PIXELFORMAT_XRGB8888 };
    *format = surface.Get()->format;
    *pixels = surface.Get()->pixels;
    *pitch  = surface.Get()->pitch;
    device->internal->Framebuffer(std::move(surface));
    return true;
  });
}
// SDL supplies a borrowed device, window and counted rectangle buffer.
auto UpdateFramebuffer(SDL_VideoDevice* device, [[maybe_unused]] SDL_Window* unused_window, SDL_Rect const* rects,
                       int count) -> bool {
  utilities::Expects(device != nullptr, "frame update has a device");
  utilities::Expects(count >= 0, "rectangle count is nonnegative");
  auto const& data    = *device->internal;
  auto const  surface = data.Framebuffer();
  if (!surface) return SDL_SetError("Couldn't find RDP surface for window");
  if (count == 0) return true;
  return Boundary([&] {
    return Present(data.Backend(), *surface, Damage(std::span(rects, static_cast<std::size_t>(count))));
  });
}
// SDL's framebuffer destruction callback borrows its device and window.
auto DestroyFramebuffer(SDL_VideoDevice* device, [[maybe_unused]] SDL_Window* unused_window) -> void {
  utilities::Expects(device != nullptr, "framebuffer destruction has a device");
  device->internal->Framebuffer(std::nullopt);
}
}
auto InitFramebuffer(SDL_VideoDevice& device) -> void {
  device.CreateWindowFramebuffer  = CreateFramebuffer;
  device.UpdateWindowFramebuffer  = UpdateFramebuffer;
  device.DestroyWindowFramebuffer = DestroyFramebuffer;
}
}
