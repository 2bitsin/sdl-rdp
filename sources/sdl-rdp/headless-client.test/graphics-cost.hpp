#pragma once
#include "graphics-backend.hpp"
#include "graphics-observer.hpp"

#include <cstdint>
#include <vector>

namespace Headless {
class GraphicsCost : public GraphicsBackend {
protected:
  auto        ThenProgressiveCost(Client& client, GraphicsObserver& observer) -> void;
  auto Open(std::uint32_t width = 1280, std::uint32_t height = 800, sdlrdp_codec codec = SDLRDP_CODEC_PROGRESSIVE)
      -> void;
  auto        PresentMovingTiles(Client& client, std::size_t frames)          -> void;
  auto        PresentPlanar(Client& client, GraphicsObserver& observer, std::vector<std::uint32_t> const& pixels,
                            std::vector<std::uint32_t> const& expected, sdlrdp_rect area) -> void;
  static auto RecordAvcCost(Logs& logs)                                       -> void;
};
}
