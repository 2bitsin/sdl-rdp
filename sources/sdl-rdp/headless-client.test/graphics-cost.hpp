#pragma once
#include "graphics-backend.hpp"
#include "graphics-observer.hpp"

#include <cstdint>
#include <vector>

namespace Headless {
class GraphicsCost : public GraphicsBackend {
protected:
  auto        ThenProgressiveCost(Client& client, GraphicsObserver& observer)                                   -> void;
  auto        AwaitAcknowledgement(Client& client, uint64_t sequence)                                           -> void;
  auto        Open(unsigned width = 1280, unsigned height = 800, sdlrdp_codec codec = SDLRDP_CODEC_PROGRESSIVE) -> void;
  auto        PresentMovingTiles(Client& client, std::uint32_t frames)                                          -> void;
  auto        PresentPlanar(Client& client, GraphicsObserver& observer, std::vector<std::uint32_t> const& pixels,
                            std::vector<std::uint32_t> const& expected, sdlrdp_rect area) -> void;
  static auto RecordAvcCost(Logs& logs)                                                                         -> void;
};
}
