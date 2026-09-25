#pragma once
#include "update-hook.hpp"
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <cstddef>

namespace sdl_rdp::headless_client_test::frame::detail::counter {
using sdl_rdp::headless_client_test::client::Client;

class FrameCounter {
public:
  explicit FrameCounter(Client& client);
  auto     Frames() const     -> std::size_t;
  auto     BitmapPdus() const -> std::size_t;

private:
  auto Count(PictureUpdate const& update) -> void;
  std::size_t       frames      = 0;
  std::size_t       bitmap_pdus = 0;
  PictureUpdateHook hook;
};
}

namespace sdl_rdp::headless_client_test::frame {
using detail::counter::FrameCounter;
}
