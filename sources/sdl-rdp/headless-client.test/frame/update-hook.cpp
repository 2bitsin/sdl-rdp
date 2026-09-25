#include <sdl-rdp/headless-client.test/frame/update-hook.hpp>

#include <sdl-rdp/headless-client.test/client/handles.hpp>
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <freerdp/gdi/gdi.h>
#include <freerdp/update.h>
#include <cstdint>
#include <ranges>
#include <utility>
#include <vector>

namespace sdl_rdp::headless_client_test::frame::detail::update_hook {
using sdl_rdp::headless_client_test::client::ClientUpdates;
using sdl_rdp::headless_client_test::utilities::ObserverSet;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Rect;

namespace {
auto Corners(std::uint32_t left, std::uint32_t top, std::uint32_t right, std::uint32_t bottom) -> Rect {
  return { .x = static_cast<int>(left),
           .y = static_cast<int>(top),
           .w = static_cast<int>(right) - static_cast<int>(left),
           .h = static_cast<int>(bottom) - static_cast<int>(top) };
}
auto Regions(SURFACE_BITS_COMMAND const& command) -> std::vector<Rect> {
  return { Corners(command.destLeft, command.destTop, command.destRight, command.destBottom) };
}
auto Regions(BITMAP_UPDATE const& command) -> std::vector<Rect> {
  // Bitmap update corners are inclusive; surface command corners are exclusive.
  return std::span(command.rectangles, command.number) | std::views::transform([](auto const& rectangle) {
           return Corners(rectangle.destLeft, rectangle.destTop, rectangle.destRight + 1, rectangle.destBottom + 1);
         })
         | std::ranges::to<std::vector>();
}
auto Desktop(rdpContext const& context) -> Extent {
  Expects(context.gdi != nullptr, "decoded framebuffer exists");
  return { .width  = static_cast<std::uint32_t>(context.gdi->width),
           .height = static_cast<std::uint32_t>(context.gdi->height) };
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
  // abi: pSurfaceBits and pBitmapUpdate, BOOL is int
  template <auto original, PictureCommand command, class Wire>
  static auto Receive(rdpContext* context, Wire const* wire) -> int;
  rdpUpdate&    update;
  pSurfaceBits  surface;
  pBitmapUpdate bitmap;
  Observer      observer;
};

PictureUpdateHook::Installation::Installation(Client& client, Observer observer)
    : update(ClientUpdates(client)), surface(update.SurfaceBits), bitmap(update.BitmapUpdate),
      observer(std::move(observer)) {
  Expects(surface, "surface callback is installed");
  Expects(bitmap, "bitmap callback is installed");
  ObserverSet::Of(*update.context).Add(*this);
  update.SurfaceBits  = Receive<&Installation::surface, PictureCommand::Surface>;
  update.BitmapUpdate = Receive<&Installation::bitmap, PictureCommand::Bitmap>;
}
PictureUpdateHook::Installation::~Installation() {
  update.SurfaceBits  = surface;
  update.BitmapUpdate = bitmap;
  ObserverSet::Of(*update.context).Remove<Installation>();
}
template <auto original, PictureCommand command, class Wire>
auto PictureUpdateHook::Installation::Receive(rdpContext* context, Wire const* wire) -> int {
  Expects(context != nullptr, "callback context exists");
  Expects(wire != nullptr, "wire command is supplied");
  auto const active  = ObserverSet::Of(*context).Held<Installation>();
  auto       result  = ((*active).*original)(context, wire);
  auto const regions = Regions(*wire);
  auto const desktop = Desktop(*context);
  active->observer({ .command = command, .regions = regions, .desktop = desktop, .delivered = result != 0 });
  return result;
}

PictureUpdateHook::PictureUpdateHook(Client& client, Observer observer)
    : installation(std::make_unique<Installation>(client, std::move(observer))) { }
PictureUpdateHook::~PictureUpdateHook() = default;
}
