#pragma once
#include "backend.hpp"
#include "observer.hpp"

#include <cstdint>
#include <vector>

namespace sdl_rdp::headless_client_test::graphics::detail::cost {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Pixels;

class GraphicsCost : public GraphicsBackend {
protected:
  auto Open(std::uint32_t width = 1280, std::uint32_t height = 800, sdlrdp_codec codec = SDLRDP_CODEC_PROGRESSIVE)
      -> void;
  auto PresentMovingTiles(Client& client, std::size_t frames) -> void;
  auto PresentPlanar(Client& client, GraphicsObserver& observer, Pixels const& pixels, Pixels const& expected,
                     sdlrdp_rect area) -> void;
};
}

namespace sdl_rdp::headless_client_test::graphics {
using detail::cost::GraphicsCost;
}
