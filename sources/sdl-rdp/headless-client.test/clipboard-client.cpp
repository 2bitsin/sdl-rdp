#include <sdl-rdp/headless-client.test/clipboard-client.hpp>

#include <sdl-rdp/clipboard/clipboard-capabilities.hpp>
#include <sdl-rdp/headless-client.test/client-channels.hpp>

#include <freerdp/addin.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/channels.h>
#include <winpr/clipboard.h>
#include <algorithm>
#include <ranges>
#include <span>
#include <string_view>
#include <utility>

namespace Headless {
namespace {
auto HeldClipboard(CliprdrClientContext* context) -> ClipboardClient& {
  Expects(context, "callback context exists");
  Expects(context->custom, "callback context carries its observer");
  return *static_cast<ClipboardClient*>(context->custom);
}
}

ClipboardClient::ClipboardClient(Client& value, std::vector<BYTE> initial)
    : client(value), outgoing(std::move(initial)) {
  Expects(!attaching, "one clipboard client per connecting thread");
  attaching = this;
  freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
  auto* context = client.Instance()->context;
  Expects(freerdp_settings_set_bool(context->settings, FreeRDP_RedirectClipboard, TRUE), "clipboard enabled");
  PubSub_SubscribeChannelConnected(context->pubSub, Connected);
  client.Instance()->LoadChannels = [](freerdp* instance) -> BOOL {
    return LoadStaticChannel(instance, CLIPRDR_SVC_CHANNEL_NAME);
  };
}
ClipboardClient::~ClipboardClient() {
  freerdp_disconnect(client.Instance().get());
  PubSub_UnsubscribeChannelConnected(client.Instance()->context->pubSub, Connected);
  attaching = nullptr;
}
auto ClipboardClient::Received(std::vector<BYTE> const& bytes) -> bool {
  std::scoped_lock const lock(guard);
  return incoming == bytes && std::ranges::contains(formats, CF_UNICODETEXT) && std::ranges::contains(formats, CF_TEXT);
}
auto ClipboardClient::RequestFormat(UINT32 format) -> UINT {
  Expects(channel.load(), "clipboard channel connected");
  CLIPRDR_FORMAT_DATA_REQUEST request{ .common = { .msgType = CB_FORMAT_DATA_REQUEST } };
  request.requestedFormatId = format;
  return channel.load()->ClientFormatDataRequest(channel.load(), &request);
}
auto ClipboardClient::Offer(std::vector<BYTE> bytes, bool unicode) -> UINT {
  Expects(channel.load(), "clipboard channel connected");
  {
    std::scoped_lock const lock(guard);
    outgoing = std::move(bytes);
  }
  CLIPRDR_FORMAT      format{ UINT32(unicode ? CF_UNICODETEXT : CF_DIB), nullptr };
  CLIPRDR_FORMAT_LIST list  { .common = { .msgType = CB_FORMAT_LIST }            };
  list.numFormats = 1;
  list.formats    = &format;
  return channel.load()->ClientFormatList(channel.load(), &list);
}
auto ClipboardClient::Observed() const -> ClipboardCapture const& {
  return observed;
}
auto ClipboardClient::Connected(void* /*unused*/, ChannelConnectedEventArgs const* event) -> void {
  if (std::string_view(event->name) != CLIPRDR_SVC_CHANNEL_NAME) return;
  Expects(attaching, "one clipboard attachment per connecting thread");
  auto* context = static_cast<CliprdrClientContext*>(event->pInterface);
  context->custom                   = attaching;
  context->MonitorReady             = Ready;
  context->ServerFormatList         = Formats;
  context->ServerFormatDataRequest  = Request;
  context->ServerFormatListResponse = [](CliprdrClientContext* ctx, CLIPRDR_FORMAT_LIST_RESPONSE const*) -> UINT {
    ++HeldClipboard(ctx).observed.accepted;
    return CHANNEL_RC_OK;
  };
  context->ServerFormatDataResponse = Response;
  attaching->channel                = context;
}
auto ClipboardClient::Ready(CliprdrClientContext* context, CLIPRDR_MONITOR_READY const* /*unused*/) -> UINT {
  auto result = Backend::SendGeneralCapabilities(
      [&](auto const* caps) { return context->ClientCapabilities(context, caps); });
  if (result != CHANNEL_RC_OK) return result;
  auto& self = HeldClipboard(context);
  if (!self.outgoing.empty()) return self.Offer(self.outgoing);
  CLIPRDR_FORMAT_LIST const list{ .common = { .msgType = CB_FORMAT_LIST } };
  return context->ClientFormatList(context, &list);
}
auto ClipboardClient::Formats(CliprdrClientContext* context, CLIPRDR_FORMAT_LIST const* list) -> UINT {
  auto& self = HeldClipboard(context);
  {
    std::scoped_lock const lock(self.guard);
    self.formats = std::span(list->formats, list->numFormats) | std::views::transform(&CLIPRDR_FORMAT::formatId)
                   | std::ranges::to<std::vector>();
  }
  CLIPRDR_FORMAT_LIST_RESPONSE response{ .common = { .msgType = CB_FORMAT_LIST_RESPONSE } };
  response.common.msgFlags = CB_RESPONSE_OK;
  auto result = context->ClientFormatListResponse(context, &response);
  if (result != CHANNEL_RC_OK) return result;
  CLIPRDR_FORMAT_DATA_REQUEST request{ .common = { .msgType = CB_FORMAT_DATA_REQUEST } };
  request.requestedFormatId = CF_UNICODETEXT;
  return context->ClientFormatDataRequest(context, &request);
}
auto ClipboardClient::Request(CliprdrClientContext* context, CLIPRDR_FORMAT_DATA_REQUEST const* request) -> UINT {
  auto&                        self     = HeldClipboard(context);
  std::scoped_lock const       lock(self.guard);
  CLIPRDR_FORMAT_DATA_RESPONSE response { .common = { .msgType = CB_FORMAT_DATA_RESPONSE } };
  response.common.msgFlags     = request->requestedFormatId == CF_UNICODETEXT ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
  response.common.dataLen      = self.outgoing.size();
  response.requestedFormatData = self.outgoing.data();
  ++self.observed.requests;
  return context->ClientFormatDataResponse(context, &response);
}
auto ClipboardClient::Response(CliprdrClientContext* context, CLIPRDR_FORMAT_DATA_RESPONSE const* response) -> UINT {
  auto&                  self = HeldClipboard(context);
  std::scoped_lock const lock(self.guard);
  if (response->common.msgFlags & CB_RESPONSE_OK)
    self.incoming.assign(response->requestedFormatData, response->requestedFormatData + response->common.dataLen);
  ++self.observed.responses;
  return CHANNEL_RC_OK;
}
}
