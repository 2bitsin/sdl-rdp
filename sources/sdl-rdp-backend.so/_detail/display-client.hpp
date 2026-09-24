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
              DisplayClient(DisplayClient const&)                                                    = delete;
              DisplayClient(DisplayClient&&)                                                         = delete;
  explicit    DisplayClient(Client& client);
              ~DisplayClient();
  auto        operator = (DisplayClient const&)                                    -> DisplayClient& = delete;
  auto        operator = (DisplayClient&&)                                         -> DisplayClient& = delete;
  static auto Monitor(unsigned width, unsigned height, unsigned millimetres = 400) -> DISPLAY_CONTROL_MONITOR_LAYOUT;
  static auto Layout(unsigned width, unsigned height)                              -> bool;
  auto        Observed()                                                           -> DisplayCapture&;
  static auto Ready()                                                              -> bool;
  static auto Channel()                                                            -> DispClientContext*;

private:
  static auto Resize(rdpContext* context)                                         -> BOOL;
  static auto Connected(void* /*unused*/, ChannelConnectedEventArgs const* event) -> void;
  DisplayCapture                                observed;
  inline static thread_local DisplayClient*     active         = nullptr;
  Client&                                       client;
  pDesktopResize                                desktop_resize;
  inline static std::atomic<DispClientContext*> channel        = nullptr;
  inline static std::atomic_bool                ready          = false;
};
}
