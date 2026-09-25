#pragma once
#include <sdl-rdp/SDL3/rdp/backend/sdl-internals.hpp>
#include <sdl-rdp/SDL3/rdp/driver.hpp>

#include <optional>
#include <string>
namespace sdl3::rdp::video::detail::device {
auto SetAspect(Driver const& driver, std::optional<std::string> const& value)   -> void;
auto PublishAspect(SDL_Window& window, std::optional<std::string> const& value) -> void;
}
namespace sdl3::rdp::video {
using detail::device::SetAspect;
using detail::device::PublishAspect;
}
