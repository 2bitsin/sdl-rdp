#include "mouse.hpp"
#include <sdl-rdp/SDL3/rdp/driver.hpp>
#include <sdl-rdp/SDL3/rdp/sdl/boundary.hpp>
#include <sdl-rdp/SDL3/rdp/sdl/resources.hpp>
#include <sdl-rdp/SDL3/rdp/video/videodata.hpp>
#include <sdl-rdp/session/backend.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/video/pointer/layout.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>

using sdl3::rdp::sdl::ConvertedSurface;

// SDL declares this tag as a struct; the members stay private.
struct SDL_CursorData {
public:
       SDL_CursorData(SDL_Surface& source, int hot_x, int hot_y)
      : _surface{ &source, SDL_PIXELFORMAT_ARGB8888 }, _hot_x{ hot_x }, _hot_y{ hot_y } { }
  auto Surface() const -> SDL_Surface const& {
    return *_surface.Get();
  }
  auto HotX() const -> int {
    return _hot_x;
  }
  auto HotY() const -> int {
    return _hot_y;
  }
private:
  ConvertedSurface const _surface;
  int                    _hot_x;
  int                    _hot_y;
};
namespace sdl3::rdp::input::detail::mouse {
using sdl3::rdp::sdl::Boundary;
using sdl3::rdp::sdl::Surface;
using sdl3::rdp::video::CurrentVideo;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::video::pointer::PointerLayout;
namespace {
auto SetPointer(Driver& driver, SDL_CursorData const& shape) -> void {
  auto const&         surface = shape.Surface();
  PointerLayout const layout  { { .width  = Narrowed<std::uint32_t>(surface.w),
                                  .height = Narrowed<std::uint32_t>(surface.h) },
                                Narrowed<std::uint32_t>(shape.HotX()),
                                Narrowed<std::uint32_t>(shape.HotY()) };
  driver.Backend().Presentation().SetPointer(layout,
                                             { static_cast<std::uint8_t const*>(surface.pixels), layout.Bytes() });
}
auto HidePointer(Driver& driver) -> void {
  driver.Backend().Presentation().SetPointer({ { }, 0, 0 }, { });
}
// SDL returns cursor ownership through this destruction callback.
auto FreeCursor(SDL_Cursor* cursor) -> void {
  Expects(cursor != nullptr, "cursor destruction owns a cursor");
  std::unique_ptr<SDL_Cursor> const     owner{ cursor                                  };
  std::unique_ptr<SDL_CursorData> const state{ std::exchange(owner->internal, nullptr) };
}
// SDL lends the source surface and takes ownership of the created cursor.
auto CreateCursor(SDL_Surface* surface, int hot_x, int hot_y) -> SDL_Cursor* {
  Expects(surface != nullptr, "cursor creation has a surface");
  return Boundary([&] {
           auto cursor = std::make_unique<SDL_Cursor>();
           cursor->internal = std::make_unique<SDL_CursorData>(*surface, hot_x, hot_y).release();
           return cursor;
         })
      .release();
}
// SDL's context-free cursor callback borrows an optional cursor; its video accessor supplies the device.
auto ShowPointer(Driver& driver, SDL_Cursor const& cursor) -> void {
  Expects(cursor.internal != nullptr, "a shown cursor has its image");
  SetPointer(driver, *cursor.internal);
}
auto ShowCursor(SDL_Cursor* cursor) -> bool {
  auto& driver = CurrentVideo().Driver();
  return Boundary([&] {
    if (cursor)
      ShowPointer(driver, *cursor);
    else
      HidePointer(driver);
    return true;
  });
}
}
auto InitMouse() -> void {
  auto& mouse = *SDL_GetMouse();
  mouse.CreateCursor = CreateCursor;
  mouse.ShowCursor   = ShowCursor;
  mouse.FreeCursor   = FreeCursor;
}
}
