#pragma once
#include "graphics-observer.hpp"
#include "test-backend-events.hpp"

#include <atomic>
#include <cstddef>
#include <vector>

namespace BackendGate {
class FrameChecks : protected BackendEvents {
protected:
  void        Present(std::vector<UINT32> const& pixels, unsigned w, unsigned h);
  void        FillLegacyWindow(Client& client, FrameObserver& observer, std::vector<UINT32>& pixels);
  void        SuppressAndCheckInput(Client& client);
  static void ThenDesktopGeometry(Client const& client, unsigned w, unsigned h);
  void        ThenAspectGeometry(Client& client);
  static void ThenScaledHighlight(Client& client);
  void        ThenSparseDamage(Client& client, FrameObserver& observer, std::vector<UINT32> const& pixels,
                               std::size_t bounding, sdlrdp_codec codec);
  static void ThenProducerFrame(Client& client, FrameObserver& observer, std::atomic<unsigned> const& presents);
  static void ThenReadable(Client const& client);
  void        ThenQoe(Client& client, Headless::GraphicsObserver& observer);
  void        ResizePicture(Client& client, Headless::GraphicsObserver& observer, std::vector<UINT32>& pixels,
                            unsigned w, unsigned h, bool graphics);
};
}
