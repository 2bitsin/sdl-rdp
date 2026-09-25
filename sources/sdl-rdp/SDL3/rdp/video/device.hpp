#pragma once
#include <sdl-rdp/SDL3/rdp/backend/sdl-internals.hpp>
#include <sdl-rdp/SDL3/rdp/driver.hpp>
#include <sdl-rdp/settings/aspect.hpp>
namespace sdl3::rdp::video::detail::device {
auto SetAspect(Driver const& driver, sdl_rdp::settings::Aspect const& value)   -> void;
auto PublishAspect(SDL_Window& window, sdl_rdp::settings::Aspect const& value) -> void;
}
namespace sdl3::rdp::video {
using detail::device::SetAspect;
using detail::device::PublishAspect;
}
