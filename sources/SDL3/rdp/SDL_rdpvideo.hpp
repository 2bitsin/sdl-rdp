#pragma once
#include "SDL_rdpvideodata.hpp"
namespace rdp {
auto InitMouse()                                                                -> void;
auto InitEvents(SDL_VideoDevice& device)                                        -> void;
auto InitWindow(SDL_VideoDevice& device)                                        -> void;
auto InitFramebuffer(SDL_VideoDevice& device)                                   -> void;
auto InitClipboard(SDL_VideoDevice& device)                                     -> void;
auto ClipboardUpdate(SDL_VideoData const& data)                                 -> void;
auto DesktopMode(SDL_VideoData const& data, int width, int height)              -> void;
auto ResizePicture(SDL_VideoData& data, int width, int height)                  -> bool;
auto SetAspect(Driver const& driver, std::optional<std::string> const& value)   -> void;
auto PublishAspect(SDL_Window& window, std::optional<std::string> const& value) -> void;
}
