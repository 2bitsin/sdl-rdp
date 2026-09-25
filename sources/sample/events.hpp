#pragma once
#include <SDL3/SDL.h>
#include <cstdint>

namespace sample::detail::events {
auto PrintAudioFormat(SDL_AudioDeviceID device)                                  -> void;
auto PrintEvent(SDL_Event const& event, SDL_Window& window, std::uint32_t frame) -> void;
}

namespace sample {
using detail::events::PrintAudioFormat;
using detail::events::PrintEvent;
}
