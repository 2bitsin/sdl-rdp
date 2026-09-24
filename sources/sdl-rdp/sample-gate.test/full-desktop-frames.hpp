#pragma once
#include <sdl-rdp/headless-client.test/client.hpp>
#include <sdl-rdp/headless-client.test/picture-update-hook.hpp>
#include <cstddef>
#include <vector>

namespace SampleGate {
class FullDesktopFrames {
public:
  explicit FullDesktopFrames(Headless::Client& client);
  auto     Full() const       -> std::size_t;
  auto     Deliveries() const -> std::size_t;

private:
  auto Observe(Headless::PictureUpdate const& update)     -> void;
  auto Cover(sdlrdp_rect region, Backend::Extent desktop) -> void;
  std::size_t                 full       = 0;
  std::size_t                 deliveries = 0;
  std::vector<bool>           rows;
  Headless::PictureUpdateHook hook;
};
}
