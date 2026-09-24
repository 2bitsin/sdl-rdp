#include "_detail/callback-owner.hpp"
#include "_detail/input-events.hpp"
#include "_detail/input.hpp"
#include "_detail/peer-link.hpp"

#include <freerdp/channels/wtsvc.h>

namespace Backend {
namespace {
auto TouchHandled(uint32_t result) -> bool {
  switch (result) {
  case CHANNEL_RC_OK:
  // FreeRDP 3.15 channels/rdpei/server/rdpei_main.c:701 maps ERROR_NO_DATA to ERROR_READ_FAULT.
  case ERROR_READ_FAULT:
    return true;
  default:
    return false;
  }
}
}
Input::Input(PeerLink& link, InputEvents& events) noexcept : _link{ link }, _events{ events } { }
auto Input::InstallChannels() -> void {
  _advanced->data              = this;
  _advanced->rdpcontext        = &_link.Context();
  _advanced->MouseEvent        = Advanced;
  _advanced->ChannelIdAssigned = [](ainput_server_context* context, UINT32 id) -> BOOL {
    CallbackOwner<Input>(context->data)._advanced_id = id;
    return TRUE;
  };
  _touch->user_data            = this;
  _touch->onTouchEvent         = Touch;
  _touch->onChannelIdAssigned  = [](RdpeiServerContext* context, UINT32 id) -> BOOL {
    CallbackOwner<Input>(context->user_data)._touch_id = id;
    return TRUE;
  };
}
auto Input::Open() -> bool {
  _opened = true;
  _link.Invalidate();
  _advanced.reset(ainput_server_context_new(_link.Channels()));
  _touch.reset(rdpei_server_context_new(_link.Channels()));
  if (!_advanced || !_touch) return false;
  InstallChannels();
  return _advanced->Initialize(_advanced.get(), TRUE) == CHANNEL_RC_OK &&
         _advanced->Open(_advanced.get()) == CHANNEL_RC_OK && _advanced->Poll(_advanced.get()) == CHANNEL_RC_OK &&
         _advanced->ChannelHandle(_advanced.get(), &_advanced_event) &&
         rdpei_server_init(_touch.get()) == CHANNEL_RC_OK;
}
auto Input::Channels(std::span<HANDLE const> ready) -> bool {
  if (!DynamicChannelsReady(_link)) return true;
  if (!_opened) return Open();
  if (_advanced_ready && std::ranges::contains(ready, _advanced_event) &&
      _advanced->Poll(_advanced.get()) != CHANNEL_RC_OK)
    return false;
  if (!_touch_ready || !std::ranges::contains(ready, rdpei_server_get_event_handle(_touch.get()))) return true;
  return TouchHandled(rdpei_server_handle_messages(_touch.get()));
}
auto Input::Handles(std::span<HANDLE> out) const -> std::span<HANDLE> {
  Expects(out.size() >= InputHandleLimit, "handle span has room for the input channels");
  auto next = out.begin();
  if (_advanced_ready) *next++ = _advanced_event;
  if (_touch_ready) *next++ = rdpei_server_get_event_handle(_touch.get());
  return { next, out.end() };
}
auto Input::Activate(UINT32 channel_id) -> std::optional<BOOL> {
  if (_advanced_id == channel_id) {
    _advanced_ready = true;
    return _advanced->Poll(_advanced.get()) == CHANNEL_RC_OK;
  }
  if (_touch_id == channel_id) {
    _touch_ready = true;
    return rdpei_server_send_sc_ready(_touch.get(), RDPINPUT_PROTOCOL_V10, 0) == CHANNEL_RC_OK;
  }
  return std::nullopt;
}
auto Input::Advanced(ainput_server_context* context, UINT64 /*unused*/, UINT64 flags, INT32 x, INT32 y) -> UINT {
  Expects(context != nullptr, "callback context exists");
  return CallbackOwner<Input>(context->data)._events.Pointer(flags, x, y);
}
auto Input::Touch(RdpeiServerContext* context, RDPINPUT_TOUCH_EVENT const* event) -> UINT {
  Expects(context != nullptr, "callback context exists");
  Expects(event != nullptr, "event is supplied");
  return CallbackOwner<Input>(context->user_data)._events.Touch(*event);
}
}
