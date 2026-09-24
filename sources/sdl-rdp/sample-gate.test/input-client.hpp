#pragma once
#include <freerdp/client/ainput.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/rdpei.h>
#include <sdl-rdp/headless-client.test/client.hpp>
#include <atomic>

namespace SampleGate {
struct InputClient {
public:
  explicit    InputClient(Headless::Client& client);
  static auto Advanced() -> std::atomic<AInputClientContext*> const&;
  static auto Touch()    -> std::atomic<RdpeiClientContext*> const&;

private:
  static auto Connected(void* /*unused*/, ChannelConnectedEventArgs const* event) -> void;
  inline static std::atomic<AInputClientContext*> advanced = nullptr;
  inline static std::atomic<RdpeiClientContext*>  touch    = nullptr;
};
}
