#pragma once
#include <sdl-rdp/SDL3/rdp/backend/sdl-internals.hpp>
namespace sdl3::rdp::video::detail::events {
auto InitEvents(SDL_VideoDevice& device) -> void;
}
namespace sdl3::rdp::video {
using detail::events::InitEvents;
}
