#pragma once
#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/headless-client.test/graphics/observer.hpp>
#include <sdl-rdp/utilities/extent.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sdl_rdp::headless_client_test::frame::detail::checks {
using sdl_rdp::headless_client_test::backend::BackendEvents;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::graphics::GraphicsObserver;
using sdl_rdp::utilities::Extent;

class FrameChecks : protected BackendEvents {
protected:
  auto        Present(Pixels const& pixels, std::uint32_t w, std::uint32_t h)           -> void;
  auto        FillLegacyWindow(Client& client, FrameObserver& observer, Pixels& pixels) -> void;
  auto PresentObserved(Client& client, FrameObserver const& observer, Pixels const& pixels, std::size_t frames) -> void;
  auto        SuppressAndCheckInput(Client& client)                                     -> void;
  static auto ThenDesktopGeometry(Client& client, std::uint32_t w, std::uint32_t h)     -> void;
  auto        ThenAspectGeometry(Client& client)                                        -> void;
  static auto ThenScaledHighlight(Client& client)                                       -> void;
  auto        ThenSparseDamage(Client& client, FrameObserver& observer, Pixels const& pixels, std::size_t bounding,
                               sdlrdp_codec codec) -> void;
  static auto ThenProducerFrame(Client& client, FrameObserver& observer, std::atomic<std::size_t> const& presents)
      -> void;
  static auto ThenReadable(Client& client)                                              -> void;
  auto        ThenQoe(Client& client, GraphicsObserver& observer)                       -> void;
  auto ResizePicture(Client& client, GraphicsObserver& observer, Pixels& pixels, Extent size, bool graphics) -> void;
};
}

namespace sdl_rdp::headless_client_test::frame {
using detail::checks::FrameChecks;
}
