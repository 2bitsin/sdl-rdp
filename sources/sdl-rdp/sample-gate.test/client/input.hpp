#pragma once
#include <freerdp/client/ainput.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/rdpei.h>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <atomic>

namespace sdl_rdp::sample_gate_test::client::detail::input {
using sdl_rdp::headless_client_test::client::Client;

struct InputClient {
public:
  explicit    InputClient(Client& client);
  static auto Advanced() -> std::atomic<AInputClientContext*> const&;
  static auto Touch()    -> std::atomic<RdpeiClientContext*> const&;

private:
  inline static std::atomic<AInputClientContext*> advanced = nullptr;
  inline static std::atomic<RdpeiClientContext*>  touch    = nullptr;
};
}

namespace sdl_rdp::sample_gate_test::client {
using detail::input::InputClient;
}
