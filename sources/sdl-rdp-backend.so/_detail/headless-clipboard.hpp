#pragma once
#include "headless-client.hpp"

#include <freerdp/client/cliprdr.h>
#include <mutex>
#include <span>
#include <winpr/clipboard.h>

namespace Headless {
struct ClipboardCapture {
  std::atomic_uint requests = 0, responses = 0, accepted = 0;
};
class ClipboardClient;
inline ClipboardClient& HeldClipboard(CliprdrClientContext* context) {
  Expects(context, "callback context exists");
  Expects(context->custom, "callback context carries its observer");
  return *static_cast<ClipboardClient*>(context->custom);
}
class ClipboardClient {
public:
  ClipboardClient(ClipboardClient const&) = delete;
  ClipboardClient(ClipboardClient&&)      = delete;
  explicit ClipboardClient(Client& value, std::vector<BYTE> initial = {})
      : client(value), outgoing(std::move(initial)) {
    Expects(!attaching, "one clipboard client per connecting thread");
    attaching = this;
    freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
    auto* context = client.Instance()->context;
    Expects(freerdp_settings_set_bool(context->settings, FreeRDP_RedirectClipboard, TRUE), "clipboard enabled");
    PubSub_SubscribeChannelConnected(context->pubSub, Connected);
    client.Instance()->LoadChannels = [](freerdp* instance) -> BOOL {
      auto* settings = instance->context->settings;
      auto entry     = reinterpret_cast<PVIRTUALCHANNELENTRYEX>(freerdp_load_channel_addin_entry(
          CLIPRDR_SVC_CHANNEL_NAME, nullptr, nullptr, FREERDP_ADDIN_CHANNEL_STATIC | FREERDP_ADDIN_CHANNEL_ENTRYEX));
      return entry && freerdp_channels_client_load_ex(instance->context->channels, settings, entry, settings) == 0;
    };
  }
  ~ClipboardClient() {
    freerdp_disconnect(client.Instance().get());
    PubSub_UnsubscribeChannelConnected(client.Instance()->context->pubSub, Connected);
    attaching = nullptr;
  }
  ClipboardClient& operator = (ClipboardClient const&) = delete;
  ClipboardClient& operator = (ClipboardClient&&)      = delete;
  bool Received(std::vector<BYTE> const& bytes) {
    std::scoped_lock const lock(guard);
    return incoming == bytes && std::ranges::contains(formats, CF_UNICODETEXT) &&
           std::ranges::contains(formats, CF_TEXT);
  }
  UINT RequestFormat(UINT32 format) {
    Expects(channel.load(), "clipboard channel connected");
    CLIPRDR_FORMAT_DATA_REQUEST request{ .common = { .msgType = CB_FORMAT_DATA_REQUEST } };
    request.requestedFormatId = format;
    return channel.load()->ClientFormatDataRequest(channel.load(), &request);
  }
  UINT Offer(std::vector<BYTE> bytes, bool unicode = true) {
    Expects(channel.load(), "clipboard channel connected");
    {
      std::scoped_lock const lock(guard);
      outgoing = std::move(bytes);
    }
    CLIPRDR_FORMAT format{ UINT32(unicode ? CF_UNICODETEXT : CF_DIB), nullptr };
    CLIPRDR_FORMAT_LIST list{ .common = { .msgType = CB_FORMAT_LIST } };
    list.numFormats = 1;
    list.formats    = &format;
    return channel.load()->ClientFormatList(channel.load(), &list);
  }
  ClipboardCapture const& Observed() const { return observed; }

private:
  static void Connected(void* /*unused*/, ChannelConnectedEventArgs const* event) {
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
  static UINT Ready(CliprdrClientContext* context, CLIPRDR_MONITOR_READY const* /*unused*/) {
    CLIPRDR_GENERAL_CAPABILITY_SET general{ CB_CAPSTYPE_GENERAL, CB_CAPSTYPE_GENERAL_LEN, CB_CAPS_VERSION_2,
                                            CB_USE_LONG_FORMAT_NAMES };
    CLIPRDR_CAPABILITIES caps{ .common = { .msgType = CB_CLIP_CAPS } };
    caps.cCapabilitiesSets = 1;
    caps.capabilitySets    = reinterpret_cast<CLIPRDR_CAPABILITY_SET*>(&general);
    auto result            = context->ClientCapabilities(context, &caps);
    if (result != CHANNEL_RC_OK) return result;
    auto& self = HeldClipboard(context);
    if (!self.outgoing.empty()) return self.Offer(self.outgoing);
    CLIPRDR_FORMAT_LIST const list{ .common = { .msgType = CB_FORMAT_LIST } };
    return context->ClientFormatList(context, &list);
  }
  static UINT Formats(CliprdrClientContext* context, CLIPRDR_FORMAT_LIST const* list) {
    auto& self = HeldClipboard(context);
    {
      std::scoped_lock const lock(self.guard);
      self.formats.clear();
      for (auto const& format : std::span(list->formats, list->numFormats))
        self.formats.push_back(format.formatId);
    }
    CLIPRDR_FORMAT_LIST_RESPONSE response{ .common = { .msgType = CB_FORMAT_LIST_RESPONSE } };
    response.common.msgFlags = CB_RESPONSE_OK;
    auto result              = context->ClientFormatListResponse(context, &response);
    if (result != CHANNEL_RC_OK) return result;
    CLIPRDR_FORMAT_DATA_REQUEST request{ .common = { .msgType = CB_FORMAT_DATA_REQUEST } };
    request.requestedFormatId = CF_UNICODETEXT;
    return context->ClientFormatDataRequest(context, &request);
  }
  static UINT Request(CliprdrClientContext* context, CLIPRDR_FORMAT_DATA_REQUEST const* request) {
    auto& self = HeldClipboard(context);
    std::scoped_lock const lock(self.guard);
    CLIPRDR_FORMAT_DATA_RESPONSE response{ .common = { .msgType = CB_FORMAT_DATA_RESPONSE } };
    response.common.msgFlags     = request->requestedFormatId == CF_UNICODETEXT ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
    response.common.dataLen      = self.outgoing.size();
    response.requestedFormatData = self.outgoing.data();
    ++self.observed.requests;
    return context->ClientFormatDataResponse(context, &response);
  }
  static UINT Response(CliprdrClientContext* context, CLIPRDR_FORMAT_DATA_RESPONSE const* response) {
    auto& self = HeldClipboard(context);
    std::scoped_lock const lock(self.guard);
    if (response->common.msgFlags & CB_RESPONSE_OK)
      self.incoming.assign(response->requestedFormatData, response->requestedFormatData + response->common.dataLen);
    ++self.observed.responses;
    return CHANNEL_RC_OK;
  }
  ClipboardCapture observed;
  inline static thread_local ClipboardClient* attaching = nullptr;
  Client&    client;
  std::mutex guard;
  std::vector<BYTE> outgoing, incoming;
  std::vector<UINT32> formats;
  std::atomic<CliprdrClientContext*> channel = nullptr;
};
}
