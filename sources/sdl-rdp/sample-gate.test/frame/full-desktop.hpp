#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/headless-client.test/frame/update-hook.hpp>
#include <cstddef>
#include <vector>

namespace sdl_rdp::sample_gate_test::frame::detail::full_desktop {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::frame::PictureUpdate;
using sdl_rdp::headless_client_test::frame::PictureUpdateHook;
using sdl_rdp::utilities::Extent;

class FullDesktopFrames {
public:
  explicit FullDesktopFrames(Client& client);
  auto     Full() const       -> std::size_t;
  auto     Deliveries() const -> std::size_t;

private:
  auto Observe(PictureUpdate const& update)      -> void;
  auto Cover(sdlrdp_rect region, Extent desktop) -> void;
  std::size_t       full       = 0;
  std::size_t       deliveries = 0;
  std::vector<bool> rows;
  PictureUpdateHook hook;
};
}

namespace sdl_rdp::sample_gate_test::frame {
using detail::full_desktop::FullDesktopFrames;
}
