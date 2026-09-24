#pragma once
#include "extent.hpp"
#include "graphics-observer.hpp"
#include "test-backend-events.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace BackendGate {
class FrameChecks : protected BackendEvents {
protected:
  auto        Present(std::vector<UINT32> const& pixels, unsigned w, unsigned h)                                -> void;
  auto        FillLegacyWindow(Client& client, FrameObserver& observer, std::vector<UINT32>& pixels)            -> void;
  auto        SuppressAndCheckInput(Client& client)                                                             -> void;
  static auto ThenDesktopGeometry(Client const& client, unsigned w, unsigned h)                                 -> void;
  auto        ThenAspectGeometry(Client& client)                                                                -> void;
  static auto ThenScaledHighlight(Client& client)                                                               -> void;
  auto        ThenSparseDamage(Client& client, FrameObserver& observer, std::vector<UINT32> const& pixels,
                               std::size_t bounding, sdlrdp_codec codec) -> void;
  static auto ThenProducerFrame(Client& client, FrameObserver& observer, std::atomic<unsigned> const& presents) -> void;
  static auto ThenReadable(Client const& client)                                                                -> void;
  auto        ThenQoe(Client& client, Headless::GraphicsObserver& observer)                                     -> void;
  auto        ResizePicture(Client& client, Headless::GraphicsObserver& observer, std::vector<std::uint32_t>& pixels,
                            Backend::Extent size, bool graphics) -> void;
};
}
