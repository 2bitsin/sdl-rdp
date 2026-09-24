#pragma once
#include "test-codec-session.hpp"

namespace BackendGate {
class Gate : public CodecSession {
protected:
  void        ThenPictureDesktop(Client const& client);
  void        WhenBurstPictures(Client& client, sdlrdp_rect area);
  static void ThenInitialScreen(sdlrdp_event const& event);
  void        ThenResizedConnection();
  void        ThenCleanDisconnect();
};
}
