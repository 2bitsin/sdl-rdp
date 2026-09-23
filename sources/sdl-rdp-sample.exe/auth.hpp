#pragma once
#include <SDL3/SDL.h>
#include <string_view>

class Authenticator {
public:
  Authenticator()                     = default;
  Authenticator(Authenticator const&) = delete;
  Authenticator(Authenticator&&)      = delete;
  ~Authenticator();
  Authenticator& operator = (Authenticator const&) = delete;
  Authenticator& operator = (Authenticator&&)      = delete;
  bool Option(std::string_view option, int& index, int argc, char** argv);
  void Defaults() const;
  void Install();

private:
  static bool SDLCALL Deny(void* /*unused*/, char const* /*unused*/, char const* /*unused*/, char const* /*unused*/);
  static void SDLCALL AuthenticationLog(void* user, int category, SDL_LogPriority priority, char const* message);
  bool                  deny          = false;
  SDL_LogOutputFunction previous      = nullptr;
  void*                 previous_user = nullptr;
};
void PrintAuthentication(SDL_Window* window);
