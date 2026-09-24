#include <SDL3/SDL.h>
#include <cstdlib>

auto main() -> int
{
    if (!SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "rdp") ||
        !SDL_SetHint(SDL_HINT_RDP_PORT, "0") || !SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
        return EXIT_FAILURE;
    }
    const auto port = SDL_GetNumberProperty(
        SDL_GetDisplayProperties(SDL_GetPrimaryDisplay()), SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0);
    SDL_Log("package smoke: rdp port %" SDL_PRIs64, port);
    SDL_Quit();
    return port > 0 && port <= 65535 ? EXIT_SUCCESS : EXIT_FAILURE;
}
