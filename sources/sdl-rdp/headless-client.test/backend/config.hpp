#pragma once
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/utilities/geometry.hpp>

#include <filesystem>

namespace sdl_rdp::headless_client_test::backend::detail::config {
using sdl_rdp::configuration::Setup;
using sdl_rdp::utilities::Extent;
inline auto LoopbackConfig(std::filesystem::path const& certificates, Extent size = { .width = 320, .height = 200 })
    -> Setup {
  return { .bind = "127.0.0.1", .cert_dir = certificates, .width = size.width, .height = size.height };
}
}

namespace sdl_rdp::headless_client_test::backend {
using detail::config::LoopbackConfig;
}
