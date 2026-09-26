#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <functional>
#include <memory>
#include <span>

namespace sdl_rdp::headless_client_test::frame::detail::update_hook {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Rect;

enum class PictureCommand{ Surface, Bitmap };
struct PictureUpdate {
  PictureCommand        command;
  std::span<Rect const> regions;
  Extent                desktop;
  bool                  delivered;
};
class PictureUpdateHook : private Pinned {
public:
  using Observer = std::function<void(PictureUpdate const& update)>;

  PictureUpdateHook(Client& client, Observer observer);
  ~PictureUpdateHook();

private:
  class Installation;
  std::unique_ptr<Installation> installation;
};
}

namespace sdl_rdp::headless_client_test::frame {
using detail::update_hook::PictureCommand;
using detail::update_hook::PictureUpdate;
using detail::update_hook::PictureUpdateHook;
}
