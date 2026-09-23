#pragma once
#include <SDL3/SDL.h>
#include <string_view>
bool AuthOption(std::string_view option, int& index, int argc, char** argv);
void AuthenticationDefaults();
void InstallAuthentication();
void PrintAuthentication(SDL_Window* window);
