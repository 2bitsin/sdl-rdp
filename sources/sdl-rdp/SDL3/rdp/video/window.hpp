#pragma once
#include "videodata.hpp"
#include <sdl-rdp/SDL3/rdp/backend/sdl-internals.hpp>
namespace sdl3::rdp::video::detail::window {
auto InitWindow(SDL_VideoDevice& device)                           -> void;
auto DesktopMode(SDL_VideoData const& data, int width, int height) -> void;
auto ResizePicture(SDL_VideoData& data, int width, int height)     -> bool;
}
namespace sdl3::rdp::video {
using detail::window::InitWindow;
using detail::window::DesktopMode;
using detail::window::ResizePicture;
}
