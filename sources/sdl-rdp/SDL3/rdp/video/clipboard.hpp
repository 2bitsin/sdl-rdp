#pragma once
#include "videodata.hpp"
namespace sdl3::rdp::video::detail::clipboard {
auto InitClipboard(SDL_VideoDevice& device) -> void;
auto ClipboardUpdate(SDL_VideoData& data)   -> void;
}

namespace sdl3::rdp::video {
using detail::clipboard::ClipboardUpdate;
using detail::clipboard::InitClipboard;
}
