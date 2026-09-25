#pragma once
#include "session.hpp"
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/utilities/rect.hpp>

namespace sdl_rdp::headless_client_test::codec::detail::gate {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::link::Event;
using sdl_rdp::utilities::Rect;

class Gate : public CodecSession {
protected:
  auto        ThenPictureDesktop(Client& client)           -> void;
  auto        WhenBurstPictures(Client& client, Rect area) -> void;
  static auto ThenInitialScreen(Event const& event)        -> void;
  auto        ThenResizedConnection()                      -> void;
  auto        ThenCleanDisconnect()                        -> void;
  auto        PresentMeasuredFrame(Client& client)         -> void;
  auto        WhenDamagedBlock(Client& client)             -> void;
  auto        ThenClientDisconnects(Client& client)        -> void;
};
}

namespace sdl_rdp::headless_client_test::codec {
using detail::gate::Gate;
}
