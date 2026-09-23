#pragma once
#include <freerdp/client/ainput.h>
#include <freerdp/client/rdpei.h>
#include <sdl-rdp-backend.so/_detail/headless-client.hpp>

namespace SampleGate {
using Headless::Client;
using utilities::Expects;
struct InputClient {
public:
  explicit InputClient(Client& client) {
    advanced = nullptr;
    touch    = nullptr;
    freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
    auto*                      context = client.Instance()->context;
    std::array<char const*, 1> ainput  { AINPUT_CHANNEL_NAME        };
    std::array<char const*, 1> rdpei   { RDPEI_CHANNEL_NAME         };
    Expects(freerdp_client_add_dynamic_channel(context->settings, 1, ainput.data()), "ainput enabled");
    Expects(freerdp_client_add_dynamic_channel(context->settings, 1, rdpei.data()), "rdpei enabled");
    PubSub_SubscribeChannelConnected(context->pubSub, Connected);
    client.Instance()->LoadChannels = [](freerdp* instance) -> BOOL {
      return freerdp_client_load_addins(instance->context->channels, instance->context->settings);
    };
  }
  static auto const& Advanced() { return advanced; }
  static auto const& Touch() { return touch; }

private:
  static void Connected(void* /*unused*/, ChannelConnectedEventArgs const* event) {
    Expects(event, "event is supplied");
    Expects(event->name, "event name is supplied");
    auto name = std::string_view(event->name);
    if (name == AINPUT_DVC_CHANNEL_NAME) advanced = static_cast<AInputClientContext*>(event->pInterface);
    if (name == RDPEI_DVC_CHANNEL_NAME) touch = static_cast<RdpeiClientContext*>(event->pInterface);
  }
  inline static std::atomic<AInputClientContext*> advanced = nullptr;
  inline static std::atomic<RdpeiClientContext*>  touch    = nullptr;
};

struct PositionObserver {
public:
  PositionObserver(PositionObserver const&) = delete;
  PositionObserver(PositionObserver&&)      = delete;
  explicit PositionObserver(Client& client) {
    Expects(!active, "one pointer observer per thread");
    active                                                       = this;
    client.Instance()->context->update->pointer->PointerPosition = Receive;
  }
  ~PositionObserver() { active = nullptr; }
  PositionObserver& operator = (PositionObserver const&) = delete;
  PositionObserver& operator = (PositionObserver&&)      = delete;
  unsigned Count() const { return count; }
  unsigned X() const { return x; }
  unsigned Y() const { return y; }

private:
  static BOOL Receive(rdpContext* /*unused*/, POINTER_POSITION_UPDATE const* position) {
    Expects(active, "observer is installed");
    Expects(position, "position observer exists");
    ++active->count;
    active->x = position->xPos;
    active->y = position->yPos;
    return TRUE;
  }
  inline static thread_local PositionObserver* active = nullptr;
  unsigned                                     count  = 0;
  unsigned                                     x      = 0;
  unsigned                                     y      = 0;
};
struct PointerObserver {
public:
  explicit PointerObserver(Client& client) {
    active                                                  = this;
    client.Instance()->context->update->pointer->PointerNew = Receive;
  }
  bool Red() const { return red; }

private:
  static BOOL Receive(rdpContext* /*unused*/, POINTER_NEW_UPDATE const* update) {
    auto const& shape = update->colorPtrAttr;
    if (shape.width != 8 || shape.height != 8 || update->xorBpp != 32) return TRUE;
    auto const* pixels = reinterpret_cast<UINT32 const*>(shape.xorMaskData);
    active->red = std::all_of(pixels, pixels + 64, [](UINT32 pixel) { return pixel == 0xffff0000; });
    return TRUE;
  }
  inline static thread_local PointerObserver* active = nullptr;
  bool                                        red    = false;
};
class FirstFrameSize {
public:
  FirstFrameSize(FirstFrameSize const&) = delete;
  FirstFrameSize(FirstFrameSize&&)      = delete;
  explicit FirstFrameSize(Client& value) : client(value), original_connect(value.Instance()->PostConnect) {
    Expects(!active, "no observer is already installed");
    Expects(original_connect, "original connection callback is installed");
    active                         = this;
    client.Instance()->PostConnect = Connect;
  }
  ~FirstFrameSize() {
    client.Instance()->PostConnect = original_connect;
    if (paint_installed) client.Instance()->context->update->EndPaint = original_paint;
    active = nullptr;
  }
  FirstFrameSize& operator = (FirstFrameSize const&) = delete;
  FirstFrameSize& operator = (FirstFrameSize&&)      = delete;
  bool Received() const { return received; }
  int Width() const { return width; }
  int Height() const { return height; }

private:
  static BOOL Connect(freerdp* instance) {
    Expects(active, "observer is installed");
    Expects(instance, "FreeRDP instance exists");
    if (!active->original_connect(instance)) return FALSE;
    active->original_paint              = instance->context->update->EndPaint;
    instance->context->update->EndPaint = Paint;
    active->paint_installed             = true;
    return TRUE;
  }
  static BOOL Paint(rdpContext* context) {
    Expects(active, "observer is installed");
    Expects(context, "callback context exists");
    Expects(context->gdi, "decoded framebuffer exists");
    if (!active->received) {
      active->width    = context->gdi->width;
      active->height   = context->gdi->height;
      active->received = true;
    }
    return active->original_paint ? active->original_paint(context) : TRUE;
  }
  bool received = false;
  int  width    = 0;
  int  height   = 0;

  inline static thread_local FirstFrameSize* active = nullptr;
  Client&                                    client;
  decltype(freerdp::PostConnect) original_connect;
  pEndPaint original_paint  = nullptr;
  bool      paint_installed = false;
};
}
