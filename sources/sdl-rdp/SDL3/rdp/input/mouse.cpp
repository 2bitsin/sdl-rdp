#include "mouse.hpp"
#include <sdl-rdp/SDL3/rdp/backend/boundary.hpp>
#include <sdl-rdp/SDL3/rdp/video/videodata.hpp>
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
  sdl3::rdp::backend::ConvertedSurface const _surface;
  int                                        _hot_x;
  int                                        _hot_y;
};
namespace sdl3::rdp::input::detail::mouse {
using backend::Boundary;
using backend::Operation;
using backend::Surface;
using video::CurrentVideo;
namespace {
auto SetPointer(Driver const& driver, SDL_CursorData const& shape) -> int {
  auto const& surface = shape.Surface();
  return driver.Call<Operation::SET_POINTER>(surface.w, surface.h, shape.HotX(), shape.HotY(), surface.pixels);
}
auto HidePointer(Driver const& driver) -> int {
  return driver.Call<Operation::SET_POINTER>(0, 0, 0, 0, nullptr);
}
// SDL returns cursor ownership through this destruction callback.
auto FreeCursor(SDL_Cursor* cursor) -> void {
  utilities::Expects(cursor != nullptr, "cursor destruction owns a cursor");
  std::unique_ptr<SDL_Cursor> const     owner{ cursor                                  };
  std::unique_ptr<SDL_CursorData> const state{ std::exchange(owner->internal, nullptr) };
}
// SDL lends the source surface and takes ownership of the created cursor.
auto CreateCursor(SDL_Surface* surface, int hot_x, int hot_y) -> SDL_Cursor* {
  utilities::Expects(surface != nullptr, "cursor creation has a surface");
  return Boundary([&] {
    auto cursor = std::make_unique<SDL_Cursor>();
    cursor->internal = std::make_unique<SDL_CursorData>(*surface, hot_x, hot_y).release();
    return cursor.release();
  });
}
// SDL's context-free cursor callback borrows an optional cursor; its video accessor supplies the device.
auto ShowPointer(Driver const& driver, SDL_Cursor const& cursor) -> int {
  utilities::Expects(cursor.internal != nullptr, "a shown cursor has its image");
  return SetPointer(driver, *cursor.internal);
}
auto ShowCursor(SDL_Cursor* cursor) -> bool {
  auto const& driver = CurrentVideo().Backend();
  auto const  result = cursor ? ShowPointer(driver, *cursor) : HidePointer(driver);
  return result == 0 || driver.Fail();
}
}
auto InitMouse() -> void {
  auto& mouse = *SDL_GetMouse();
  mouse.CreateCursor = CreateCursor;
  mouse.ShowCursor   = ShowCursor;
  mouse.FreeCursor   = FreeCursor;
}
}
