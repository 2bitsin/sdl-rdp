#pragma once
#include <sdl-rdp/abi/backend.h>

#include <string>

namespace sdl_rdp::configuration::detail::authentication {
class Authentication {
public:
           Authentication(Authentication const&)               = delete;
           Authentication(Authentication&&)                    = delete;
  explicit Authentication(sdlrdp_config const& value);
           ~Authentication();
  auto     operator=(Authentication const&) -> Authentication& = delete;
  auto     operator=(Authentication&&)      -> Authentication& = delete;
  auto     Config() const noexcept          -> sdlrdp_config const&;

private:
  sdlrdp_config _config;
  std::string   _user;
  std::string   _password;
  std::string   _domain;
};
}

namespace sdl_rdp::configuration {
using detail::authentication::Authentication;
}
