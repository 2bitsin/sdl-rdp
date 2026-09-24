#pragma once
#include "client.hpp"

#include <atomic>
#include <freerdp/client/disp.h>
#include <freerdp/event.h>
#include <functional>

namespace Headless {
struct DisplayCapture {
  bool                  echo_resize = false;
  std::function<void()> finalizing;
  unsigned              desktops    = 0;
  unsigned              echoes      = 0;
};
struct DisplayClient {
public:
                                        DisplayClient(DisplayClient const&) = delete;
                                        DisplayClient(DisplayClient&&)      = delete;
  explicit                              DisplayClient(Client& client);
                                        ~DisplayClient();
  DisplayClient&                        operator = (DisplayClient const&)   = delete;
  DisplayClient&                        operator = (DisplayClient&&)        = delete;
  static DISPLAY_CONTROL_MONITOR_LAYOUT Monitor(unsigned width, unsigned height, unsigned millimetres = 400);
  static bool                           Layout(unsigned width, unsigned height);
  DisplayCapture&                       Observed();
  static bool                           Ready();
  static DispClientContext*             Channel();

private:
  static BOOL Resize(rdpContext* context);
  static void Connected(void* /*unused*/, ChannelConnectedEventArgs const* event);
  DisplayCapture                                observed;
  inline static thread_local DisplayClient*     active         = nullptr;
  Client&                                       client;
  pDesktopResize                                desktop_resize;
  inline static std::atomic<DispClientContext*> channel        = nullptr;
  inline static std::atomic_bool                ready          = false;
};
}
