#pragma once
#include "test-codec-session.hpp"

namespace BackendGate {
class Gate : public CodecSession {
protected:
  auto        ThenPictureDesktop(Client const& client)            -> void;
  auto        WhenBurstPictures(Client& client, sdlrdp_rect area) -> void;
  static auto ThenInitialScreen(sdlrdp_event const& event)        -> void;
  auto        ThenResizedConnection()                             -> void;
  auto        ThenCleanDisconnect()                               -> void;
  auto        PresentMeasuredFrame(Client& client)                -> void;
  auto        WhenDamagedBlock(Client& client)                    -> void;
  auto        ThenClientDisconnects(Client& client)               -> void;
};
}
