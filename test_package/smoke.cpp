#include <SDL3/SDL.h>
#include <cstdlib>
#include <iostream>

int main()
{
    if (!SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp") ||
        !SDL_SetHint(SDL_HINT_RDP_PORT, "0") || !SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }
    const auto port = SDL_GetNumberProperty(
        SDL_GetDisplayProperties(SDL_GetPrimaryDisplay()), SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0);
    std::cout << "package smoke: rdp port " << port << '\n';
    SDL_Quit();
    return port > 0 && port <= 65535 ? EXIT_SUCCESS : EXIT_FAILURE;
}
