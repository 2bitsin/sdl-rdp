#pragma once
#include "sdl-rdp-backend.h"

#include <string>

namespace Backend {
class Authentication {
public:
                       Authentication(Authentication const&) = delete;
                       Authentication(Authentication&&)      = delete;
  explicit             Authentication(sdlrdp_config const& value);
                       ~Authentication();
  Authentication&      operator = (Authentication const&)    = delete;
  Authentication&      operator = (Authentication&&)         = delete;
  sdlrdp_config const& Config() const                        noexcept;

private:
  sdlrdp_config _config;
  std::string   _user;
  std::string   _password;
  std::string   _domain;
};
}
