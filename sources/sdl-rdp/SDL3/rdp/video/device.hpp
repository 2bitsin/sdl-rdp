#pragma once
#include <sdl-rdp/SDL3/rdp/driver.hpp>
#include <sdl-rdp/SDL3/rdp/sdl/internals.hpp>
#include <sdl-rdp/settings/aspect.hpp>
namespace sdl3::rdp::video::detail::device {
using sdl_rdp::settings::Aspect;

auto SetAspect(Driver& driver, Aspect const& value)         -> void;
auto PublishAspect(SDL_Window& window, Aspect const& value) -> void;
}

namespace sdl3::rdp::video {
using detail::device::PublishAspect;
using detail::device::SetAspect;
}
