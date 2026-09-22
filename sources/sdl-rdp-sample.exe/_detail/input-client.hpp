#pragma once
#include <sdl-rdp-backend.so/_detail/headless-client.hpp>
#include <freerdp/client/ainput.h>
#include <freerdp/client/rdpei.h>

namespace SampleGate {
using Headless::Client;
using utilities::Expects;
struct InputClient {
  inline static std::atomic<AInputClientContext*> advanced = nullptr;
  inline static std::atomic<RdpeiClientContext*> touch = nullptr;
  static void Connected(void*, ChannelConnectedEventArgs const* event) {
    Expects(event && event->name, "channel event exists");
    auto name = std::string_view(event->name);
    if (name == AINPUT_DVC_CHANNEL_NAME) advanced = static_cast<AInputClientContext*>(event->pInterface);
    if (name == RDPEI_DVC_CHANNEL_NAME) touch = static_cast<RdpeiClientContext*>(event->pInterface);
  }
  explicit InputClient(Client& client) {
    advanced = nullptr; touch = nullptr;
    freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
    auto context = client.instance->context;
    const char* ainput[] = {AINPUT_CHANNEL_NAME};
    const char* rdpei[] = {RDPEI_CHANNEL_NAME};
    Expects(freerdp_client_add_dynamic_channel(context->settings, 1, ainput), "ainput enabled");
    Expects(freerdp_client_add_dynamic_channel(context->settings, 1, rdpei), "rdpei enabled");
    PubSub_SubscribeChannelConnected(context->pubSub, Connected);
    client.instance->LoadChannels = [](freerdp* instance) -> BOOL {
      return freerdp_client_load_addins(instance->context->channels, instance->context->settings);
    };
  }
};

struct PositionObserver {
  inline static thread_local PositionObserver* active = nullptr;
  unsigned count = 0, x = 0, y = 0;
  explicit PositionObserver(Client& client) {
    Expects(!active, "one pointer observer per thread");
    active = this;
    client.instance->context->update->pointer->PointerPosition = Receive;
  }
  ~PositionObserver() { active = nullptr; }
  static BOOL Receive(rdpContext*, const POINTER_POSITION_UPDATE* position) {
    Expects(active && position, "pointer position exists");
    ++active->count;
    active->x = position->xPos; active->y = position->yPos;
    return TRUE;
  }
};
}
