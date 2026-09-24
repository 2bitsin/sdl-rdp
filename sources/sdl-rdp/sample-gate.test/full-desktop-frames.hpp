#pragma once
#include <sdl-rdp/headless-client.test/client.hpp>
#include <sdl-rdp/headless-client.test/picture-update-hook.hpp>
#include <vector>

namespace SampleGate {
class FullDesktopFrames {
public:
  explicit FullDesktopFrames(Headless::Client& client);
  auto     Full() const       -> unsigned;
  auto     Deliveries() const -> unsigned;

private:
  auto Observe(Headless::PictureUpdate const& update)     -> void;
  auto Cover(sdlrdp_rect region, Backend::Extent desktop) -> void;
  unsigned                    full       = 0;
  unsigned                    deliveries = 0;
  std::vector<bool>           rows;
  Headless::PictureUpdateHook hook;
};
}
