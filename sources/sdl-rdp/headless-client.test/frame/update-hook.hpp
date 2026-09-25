#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/utilities/extent.hpp>

#include <functional>
#include <memory>
#include <span>

namespace sdl_rdp::headless_client_test::frame::detail::update_hook {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::utilities::Extent;

enum class PictureCommand{ Surface, Bitmap };
struct PictureUpdate {
  PictureCommand               command;
  std::span<sdlrdp_rect const> regions;
  Extent                       desktop;
  bool                         delivered;
};
class PictureUpdateHook {
public:
  using Observer = std::function<void(PictureUpdate const& update)>;

       PictureUpdateHook(Client& client, Observer observer);
       PictureUpdateHook(PictureUpdateHook const&)               = delete;
       PictureUpdateHook(PictureUpdateHook&&)                    = delete;
       ~PictureUpdateHook();
  auto operator=(PictureUpdateHook const&) -> PictureUpdateHook& = delete;
  auto operator=(PictureUpdateHook&&)      -> PictureUpdateHook& = delete;

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
