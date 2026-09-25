#pragma once
#include "backend.hpp"
#include "observer.hpp"
#include <sdl-rdp/configuration/codec.hpp>
#include <sdl-rdp/utilities/rect.hpp>

#include <cstdint>
#include <vector>

namespace sdl_rdp::headless_client_test::graphics::detail::cost {
using sdl_rdp::configuration::Codec;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::utilities::Rect;

class GraphicsCost : public GraphicsBackend {
protected:
  auto Open(std::uint32_t width = 1280, std::uint32_t height = 800, Codec codec = Codec::Progressive) -> void;
  auto PresentMovingTiles(Client& client, std::size_t frames)                                         -> void;
  auto PresentPlanar(Client& client, GraphicsObserver& observer, Pixels const& pixels, Pixels const& expected,
                     Rect area) -> void;
};
}

namespace sdl_rdp::headless_client_test::graphics {
using detail::cost::GraphicsCost;
}
