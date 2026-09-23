#include "auth.hpp"
#include "_detail/check.hpp"
#include <array>
#include <string>

namespace {
bool deny = false;
SDL_LogOutputFunction previous;
void* previous_user;
bool SDLCALL Deny(void*, char const*, char const*, char const*) { return false; }
void SDLCALL AuthenticationLog(void*, int category, SDL_LogPriority priority, char const* message)
{
    std::string_view text(message);
    constexpr std::string_view prefix = "Authentication rejected: user \"";
    previous(previous_user, category, priority, message);
    if (!text.starts_with(prefix)) return;
    text.remove_prefix(prefix.size());
    auto end = text.find('"');
    auto name = text.substr(0, end);
    if (auto slash = name.find('\\'); slash != name.npos) name.remove_prefix(slash + 1);
    auto line = std::string("event AUTH_REJECTED user=") + std::string(name);
    previous(previous_user, category, SDL_LOG_PRIORITY_INFO, line.c_str());
}
}
bool AuthOption(std::string_view option, int& index, int argc, char** argv)
{
    if (option == "--verify-deny") { deny = true; return true; }
    for (auto [name, hint] : std::array<std::pair<std::string_view, char const*>, 4>{{
        {"--user", SDL_HINT_RDP_USER}, {"--password", SDL_HINT_RDP_PASSWORD},
        {"--domain", SDL_HINT_RDP_DOMAIN}, {"--auth", SDL_HINT_RDP_AUTH}}}) {
        if (option != name) continue;
        Check(index + 1 < argc);
        Check(SDL_SetHint(hint, argv[++index]));
        return true;
    }
    return false;
}
void AuthenticationDefaults()
{
    if (deny && !SDL_GetHint(SDL_HINT_RDP_AUTH) && !SDL_GetHint(SDL_HINT_RDP_PASSWORD))
        Check(SDL_SetHint(SDL_HINT_RDP_AUTH, "tls"));
}
void InstallAuthentication()
{
    SDL_SetLogPriority(SDL_LOG_CATEGORY_VIDEO, SDL_LOG_PRIORITY_INFO);
    SDL_GetLogOutputFunction(&previous, &previous_user);
    SDL_SetLogOutputFunction(AuthenticationLog, nullptr);
    if (deny) Check(SDL_SetPointerProperty(SDL_GetDisplayProperties(SDL_GetPrimaryDisplay()),
        SDL_PROP_DISPLAY_RDP_VERIFY_POINTER, reinterpret_cast<void*>(Deny)));
}
void PrintAuthentication(SDL_Window* window)
{
    auto properties = SDL_GetWindowProperties(window);
    SDL_Log("event CONNECTED user=%s domain=%s authenticated=%d",
        SDL_GetStringProperty(properties, SDL_PROP_WINDOW_RDP_USER_STRING, ""),
        SDL_GetStringProperty(properties, SDL_PROP_WINDOW_RDP_DOMAIN_STRING, ""),
        int(SDL_GetBooleanProperty(properties, SDL_PROP_WINDOW_RDP_AUTHENTICATED_BOOLEAN, false)));
}
