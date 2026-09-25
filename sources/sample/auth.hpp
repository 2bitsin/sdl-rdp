#pragma once
#include <SDL3/SDL.h>
#include <string_view>

namespace sample::detail::auth {
class Authenticator {
public:
       Authenticator()                                                                      = default;
       Authenticator(Authenticator const&)                                                  = delete;
       Authenticator(Authenticator&&)                                                       = delete;
       ~Authenticator();
  auto operator=(Authenticator const&)                                    -> Authenticator& = delete;
  auto operator=(Authenticator&&)                                         -> Authenticator& = delete;
  auto Option(std::string_view option, int& index, int argc, char** argv) -> bool;
  auto Defaults() const                                                   -> void;
  auto Install()                                                          -> void;

private:
  static auto SDLCALL Deny(void* /*unused*/, char const* /*unused*/, char const* /*unused*/, char const* /*unused*/)
      -> bool;
  static auto SDLCALL AuthenticationLog(void* user, int category, SDL_LogPriority priority, char const* message)
      -> void;
  bool                  deny          = false;
  SDL_LogOutputFunction previous      = nullptr;
  void*                 previous_user = nullptr;
};
auto PrintAuthentication(SDL_Window* window) -> void;
}

namespace sample {
using detail::auth::Authenticator;
using detail::auth::PrintAuthentication;
}
