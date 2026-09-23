#pragma once
#include <sdl-rdp-backend.so/_detail/headless-client.hpp>
#include <freerdp/client/ainput.h>
#include <freerdp/client/rdpei.h>

namespace SampleGate {
using Headless::Client;
using utilities::Expects;
struct InputClient {
public:
  explicit InputClient(Client& client)
  {
    advanced = nullptr;
    touch    = nullptr;
    freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
    auto* context = client.instance->context;
    const char* ainput[] = { AINPUT_CHANNEL_NAME };
    const char* rdpei[]  = { RDPEI_CHANNEL_NAME };
    Expects(freerdp_client_add_dynamic_channel(context->settings, 1, ainput), "ainput enabled");
    Expects(freerdp_client_add_dynamic_channel(context->settings, 1, rdpei), "rdpei enabled");
    PubSub_SubscribeChannelConnected(context->pubSub, Connected);
    client.instance->LoadChannels = [](freerdp* instance) -> BOOL {
      return freerdp_client_load_addins(instance->context->channels, instance->context->settings);
    };
  }
  static void Connected(void* /*unused*/, ChannelConnectedEventArgs const* event)
  {
    Expects(event && event->name, "channel event exists");
    auto name = std::string_view(event->name);
    if (name == AINPUT_DVC_CHANNEL_NAME) advanced = static_cast<AInputClientContext*>(event->pInterface);
    if (name == RDPEI_DVC_CHANNEL_NAME) touch = static_cast<RdpeiClientContext*>(event->pInterface);
  }
  inline static std::atomic<AInputClientContext*> advanced = nullptr;
  inline static std::atomic<RdpeiClientContext*>  touch    = nullptr;
};

struct PositionObserver {
public:
  PositionObserver(PositionObserver const&)            = delete;
  PositionObserver& operator=(PositionObserver const&) = delete;
  PositionObserver(PositionObserver&&)                 = delete;
  PositionObserver& operator=(PositionObserver&&)      = delete;
  explicit PositionObserver(Client& client)
  {
    Expects(!active, "one pointer observer per thread");
    active                                                     = this;
    client.instance->context->update->pointer->PointerPosition = Receive;
  }
  ~PositionObserver() { active = nullptr; }
  static BOOL Receive(rdpContext* /*unused*/, const POINTER_POSITION_UPDATE* position)
  {
    Expects(active && position, "pointer position exists");
    ++active->count;
    active->x = position->xPos;
    active->y = position->yPos;
    return TRUE;
  }
  inline static thread_local PositionObserver* active = nullptr;
  unsigned                                     count  = 0, x = 0, y = 0;
};
struct PointerObserver {
public:
  explicit PointerObserver(Client& client)
  {
    active                                                = this;
    client.instance->context->update->pointer->PointerNew = Receive;
  }
  static BOOL Receive(rdpContext* /*unused*/, POINTER_NEW_UPDATE const* update)
  {
    const auto& shape = update->colorPtrAttr;
    if (shape.width != 8 || shape.height != 8 || update->xorBpp != 32) return TRUE;
    const auto* pixels = reinterpret_cast<UINT32 const*>(shape.xorMaskData);
    active->red        = std::all_of(pixels, pixels + 64, [](UINT32 pixel) { return pixel == 0xffff0000; });
    return TRUE;
  }
  inline static thread_local PointerObserver* active = nullptr;
  bool                                        red    = false;
};
class FirstFrameSize {
public:
  FirstFrameSize(FirstFrameSize const&)            = delete;
  FirstFrameSize& operator=(FirstFrameSize const&) = delete;
  FirstFrameSize(FirstFrameSize&&)                 = delete;
  FirstFrameSize& operator=(FirstFrameSize&&)      = delete;
  explicit FirstFrameSize(Client& value) : client(value), original_connect(value.instance->PostConnect)
  {
    Expects(!active && original_connect, "one first-frame observer before connection");
    active                       = this;
    client.instance->PostConnect = Connect;
  }
  ~FirstFrameSize()
  {
    client.instance->PostConnect = original_connect;
    if (paint_installed) client.instance->context->update->EndPaint = original_paint;
    active = nullptr;
  }
  static BOOL Connect(freerdp* instance)
  {
    Expects(active && instance, "first-frame observer and client exist");
    if (!active->original_connect(instance)) return FALSE;
    active->original_paint              = instance->context->update->EndPaint;
    instance->context->update->EndPaint = Paint;
    active->paint_installed             = true;
    return TRUE;
  }
  static BOOL Paint(rdpContext* context)
  {
    Expects(active && context && context->gdi, "first-frame observer and framebuffer exist");
    if (!active->received) {
      active->width    = context->gdi->width;
      active->height   = context->gdi->height;
      active->received = true;
    }
    return active->original_paint ? active->original_paint(context) : TRUE;
  }
  bool received = false;
  int  width    = 0, height = 0;

private:
  inline static thread_local FirstFrameSize* active = nullptr;
  Client&                                    client;
  decltype(freerdp::PostConnect)             original_connect;
  pEndPaint original_paint  = nullptr;
  bool      paint_installed = false;
};
}
