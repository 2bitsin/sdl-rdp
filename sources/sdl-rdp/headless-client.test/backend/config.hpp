#pragma once
#include <sdl-rdp/abi/backend.h>

#include <string>

namespace sdl_rdp::headless_client_test::backend::detail::config {
inline auto LoopbackConfig(std::string const& certificates) -> sdlrdp_config {
  sdlrdp_config config{ };
  config.bind     = "127.0.0.1";
  config.cert_dir = certificates.c_str();
  config.width    = 320;
  config.height   = 200;
  return config;
}
auto LoopbackConfig(std::string&& certificates) -> sdlrdp_config = delete;
}

namespace sdl_rdp::headless_client_test::backend {
using detail::config::LoopbackConfig;
}
