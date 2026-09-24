#pragma once
#include "backend-events.hpp"
#include "graphics-observer.hpp"
#include <sdl-rdp/utilities/extent.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace BackendGate {
class FrameChecks : protected BackendEvents {
protected:
  auto        Present(std::vector<std::uint32_t> const& pixels, std::uint32_t w, std::uint32_t h)           -> void;
  auto        FillLegacyWindow(Client& client, FrameObserver& observer, std::vector<std::uint32_t>& pixels) -> void;
  auto        PresentObserved(Client& client, FrameObserver const& observer, std::vector<std::uint32_t> const& pixels,
                              std::size_t frames) -> void;
  auto        SuppressAndCheckInput(Client& client)                                                         -> void;
  static auto ThenDesktopGeometry(Client const& client, std::uint32_t w, std::uint32_t h)                   -> void;
  auto        ThenAspectGeometry(Client& client)                                                            -> void;
  static auto ThenScaledHighlight(Client& client)                                                           -> void;
  auto        ThenSparseDamage(Client& client, FrameObserver& observer, std::vector<std::uint32_t> const& pixels,
                               std::size_t bounding, sdlrdp_codec codec) -> void;
  static auto ThenProducerFrame(Client& client, FrameObserver& observer, std::atomic<std::size_t> const& presents)
      -> void;
  static auto ThenReadable(Client const& client)                                                            -> void;
  auto        ThenQoe(Client& client, Headless::GraphicsObserver& observer)                                 -> void;
  auto        ResizePicture(Client& client, Headless::GraphicsObserver& observer, std::vector<std::uint32_t>& pixels,
                            Backend::Extent size, bool graphics) -> void;
};
}
