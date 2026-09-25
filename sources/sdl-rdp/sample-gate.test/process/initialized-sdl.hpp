#pragma once
#include <sdl-rdp/utilities/scoped.hpp>

#include <SDL3/SDL_video.h>
#include <functional>
#include <memory>

namespace SampleGate {
auto InitializeSdl(std::function<bool()> const& initialize) -> bool;
auto QuitSdl(bool initialized) noexcept                     -> void;
// SDL_Quit runs even when an assertion returns early.
using InitializedSdl = utilities::RAIIWrap<bool, InitializeSdl, QuitSdl>;
using Window         = std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)>;
}
