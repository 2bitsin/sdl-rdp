#pragma once
#include "SDL_rdpvideodata.hpp"
namespace rdp {
void InitMouse();
void InitEvents(SDL_VideoDevice& device);
void InitWindow(SDL_VideoDevice& device);
void InitFramebuffer(SDL_VideoDevice& device);
void InitClipboard(SDL_VideoDevice& device);
void ClipboardUpdate(SDL_VideoData const& data);
void DesktopMode(SDL_VideoData const& data, int width, int height);
auto ResizePicture(SDL_VideoData& data, int width, int height) -> bool;
void SetAspect(Driver const& driver, std::optional<std::string> const& value);
void PublishAspect(SDL_Window& window, std::optional<std::string> const& value);
}
