#pragma once
#include <SDL3/SDL.h>
void PrintAudioFormat(SDL_AudioDeviceID device);
void PrintEvent(SDL_Event const& event, SDL_Window* window, unsigned frame);
