#pragma once
#include <SDL3/SDL.h>
auto PrintAudioFormat(SDL_AudioDeviceID device)                             -> void;
auto PrintEvent(SDL_Event const& event, SDL_Window* window, unsigned frame) -> void;
