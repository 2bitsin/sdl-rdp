#pragma once
#include "session.hpp"
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/utilities/geometry.hpp>

namespace sdl_rdp::headless_client_test::codec::detail::gate {
using sdl_rdp::link::Event;
using sdl_rdp::utilities::Rect;

class Gate : public CodecSession {
protected:
  auto        ThenPictureDesktop()                  -> void;
  auto        WhenBurstPictures(Rect area)          -> void;
  static auto ThenInitialScreen(Event const& event) -> void;
  auto        ThenResizedConnection()               -> void;
  auto        ThenCleanDisconnect()                 -> void;
  auto        PresentMeasuredFrame()                -> void;
  auto        WhenDamagedBlock()                    -> void;
  auto        ThenClientDisconnects()               -> void;
};
}

namespace sdl_rdp::headless_client_test::codec {
using detail::gate::Gate;
}
