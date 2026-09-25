#pragma once
#include "session.hpp"

namespace sdl_rdp::headless_client_test::codec::detail::gate {
using sdl_rdp::headless_client_test::client::Client;

class Gate : public CodecSession {
protected:
  auto        ThenPictureDesktop(Client& client)                  -> void;
  auto        WhenBurstPictures(Client& client, sdlrdp_rect area) -> void;
  static auto ThenInitialScreen(sdlrdp_event const& event)        -> void;
  auto        ThenResizedConnection()                             -> void;
  auto        ThenCleanDisconnect()                               -> void;
  auto        PresentMeasuredFrame(Client& client)                -> void;
  auto        WhenDamagedBlock(Client& client)                    -> void;
  auto        ThenClientDisconnects(Client& client)               -> void;
};
}

namespace sdl_rdp::headless_client_test::codec {
using detail::gate::Gate;
}
