#include <sdl-rdp/headless-client.test/picture-update-hook.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <freerdp/gdi/gdi.h>
#include <freerdp/update.h>
#include <cstdint>
#include <ranges>
#include <utility>
#include <vector>

namespace Headless {
using utilities::Expects;

namespace {
auto Corners(std::uint32_t left, std::uint32_t top, std::uint32_t right, std::uint32_t bottom) -> sdlrdp_rect {
  return { static_cast<int>(left), static_cast<int>(top), static_cast<int>(right) - static_cast<int>(left),
           static_cast<int>(bottom) - static_cast<int>(top) };
}
auto Regions(SURFACE_BITS_COMMAND const& command) -> std::vector<sdlrdp_rect> {
  return { Corners(command.destLeft, command.destTop, command.destRight, command.destBottom) };
}
auto Regions(BITMAP_UPDATE const& command) -> std::vector<sdlrdp_rect> {
  // Bitmap update corners are inclusive; surface command corners are exclusive.
  return std::span(command.rectangles, command.number) | std::views::transform([](auto const& rectangle) {
           return Corners(rectangle.destLeft, rectangle.destTop, rectangle.destRight + 1, rectangle.destBottom + 1);
         })
         | std::ranges::to<std::vector>();
}
auto Desktop(rdpContext const* context) -> Backend::Extent {
  Expects(context, "callback context exists");
  Expects(context->gdi, "decoded framebuffer exists");
  return { .width  = static_cast<std::uint32_t>(context->gdi->width),
           .height = static_cast<std::uint32_t>(context->gdi->height) };
}
}

class PictureUpdateHook::Installation {
public:
       Installation(Client& client, Observer observer);
       Installation(Installation const&)               = delete;
       Installation(Installation&&)                    = delete;
       ~Installation();
  auto operator=(Installation const&) -> Installation& = delete;
  auto operator=(Installation&&)      -> Installation& = delete;

private:
  template <auto original, PictureCommand command, class Wire>
  static auto Receive(rdpContext* context, Wire const* wire) -> BOOL;
  inline static thread_local Installation* active   = nullptr;
  rdpUpdate*                               update;
  pSurfaceBits                             surface;
  pBitmapUpdate                            bitmap;
  Observer                                 observer;
};

PictureUpdateHook::Installation::Installation(Client& client, Observer observer)
    : update(client.Instance()->context->update), surface(update->SurfaceBits), bitmap(update->BitmapUpdate),
      observer(std::move(observer)) {
  Expects(!active, "no observer is already installed");
  Expects(surface, "surface callback is installed");
  Expects(bitmap, "bitmap callback is installed");
  active               = this;
  update->SurfaceBits  = Receive<&Installation::surface, PictureCommand::Surface>;
  update->BitmapUpdate = Receive<&Installation::bitmap, PictureCommand::Bitmap>;
}
PictureUpdateHook::Installation::~Installation() {
  update->SurfaceBits  = surface;
  update->BitmapUpdate = bitmap;
  active               = nullptr;
}
template <auto original, PictureCommand command, class Wire>
auto PictureUpdateHook::Installation::Receive(rdpContext* context, Wire const* wire) -> BOOL {
  Expects(active, "observer is installed");
  Expects(wire, "wire command is supplied");
  auto       result  = (active->*original)(context, wire);
  auto const regions = Regions(*wire);
  auto const desktop = Desktop(context);
  active->observer({ .command = command, .regions = regions, .desktop = desktop, .delivered = result != FALSE });
  return result;
}

PictureUpdateHook::PictureUpdateHook(Client& client, Observer observer)
    : installation(std::make_unique<Installation>(client, std::move(observer))) { }
PictureUpdateHook::~PictureUpdateHook() = default;
}
