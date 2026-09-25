#include <sdl-rdp/SDL3/rdp/backend/error-routes.hpp>

#include <SDL3/SDL_error.h>
#include <gtest/gtest.h>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>

namespace sdl_rdp::integration::driver_test::detail::boundary {
namespace {
using sdl3::rdp::backend::Bounded;
using sdl3::rdp::backend::ErrorRoutes;
constexpr auto const& DriverRoutes{ ErrorRoutes<&SDL_SetError, &SDL_OutOfMemory> };
auto ErrorAfter(auto const& action) -> std::string_view {
  std::ignore = SDL_ClearError();
  EXPECT_EQ(Bounded<DriverRoutes>(action), 0);
  return SDL_GetError();
}
}
TEST(Boundary, PassesTheActionsResult) {
  EXPECT_EQ(Bounded<DriverRoutes>([] { return 7; }), 7);
}
TEST(Boundary, ReportsExhaustionAsSdlOutOfMemory) {
  std::ignore = SDL_OutOfMemory();
  std::string const expected = SDL_GetError();
  EXPECT_EQ(ErrorAfter([]() -> int { throw std::bad_alloc{ }; }), expected);
}
TEST(Boundary, ReportsAStandardExceptionsText) {
  EXPECT_EQ(ErrorAfter([]() -> int { throw std::runtime_error("drive vanished"); }), "drive vanished");
}
TEST(Boundary, NamesTheDriverForAnExceptionWithoutText) {
  EXPECT_EQ(ErrorAfter([]() -> int { throw 42; }), "Unknown exception in RDP driver");
}
TEST(Boundary, CompletesAnActionWithoutAResult) {
  std::ignore = SDL_ClearError();
  Bounded<DriverRoutes>([] { throw std::runtime_error("void failed"); });
  EXPECT_STREQ(SDL_GetError(), "void failed");
}
}
