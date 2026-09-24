#include <sdl-rdp/headless-client.test/clipboard-client.hpp>

#include <sdl-rdp/clipboard/clipboard-capabilities.hpp>
#include <sdl-rdp/headless-client.test/client-channels.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/addin.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/channels.h>
#include <winpr/clipboard.h>
#include <algorithm>
#include <cstdint>
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
auto AnnounceFormat(CliprdrClientContext& context, bool unicode) -> std::uint32_t {
  CLIPRDR_FORMAT      format{ Backend::Narrowed<std::uint32_t>(unicode ? CF_UNICODETEXT : CF_DIB), nullptr };
  CLIPRDR_FORMAT_LIST list  { .common = { .msgType = CB_FORMAT_LIST }                                      };
  list.numFormats = 1;
  list.formats    = &format;
  return context.ClientFormatList(&context, &list);
}
}
class ClipboardClient::Callbacks {
public:
  // abi: pChannelConnectedEventHandler
  static auto ChannelConnected(void* context, ChannelConnectedEventArgs const* event) -> void;
  static auto Attach(ClipboardClient& self, CliprdrClientContext& context)            -> void;
  static auto InstallFormats(CliprdrClientContext& context)                           -> void;
  static auto InstallData(CliprdrClientContext& context)                              -> void;
};
auto ClipboardClient::Callbacks::ChannelConnected(void* context, ChannelConnectedEventArgs const* event) -> void {
  Expects(event != nullptr, "channel event is supplied");
  if (std::string_view(event->name) != CLIPRDR_SVC_CHANNEL_NAME) return;
  auto* channel = static_cast<CliprdrClientContext*>(event->pInterface);
  Expects(channel != nullptr, "the clipboard channel interface exists");
  Attach(*ObserverSet::Of(context).Held<ClipboardClient>(), *channel);
}
auto ClipboardClient::Callbacks::Attach(ClipboardClient& self, CliprdrClientContext& context) -> void {
  context.custom = &self;
  InstallFormats(context);
  InstallData(context);
  self.channel = &context;
}
// abi: the pcCliprdr server message callbacks, UINT is uint32_t
auto ClipboardClient::Callbacks::InstallFormats(CliprdrClientContext& context) -> void {
  context.MonitorReady             = [](CliprdrClientContext* ctx, CLIPRDR_MONITOR_READY const*) -> std::uint32_t {
    return HeldClipboard(ctx).Ready(*ctx);
  };
  context.ServerFormatList         = [](CliprdrClientContext* ctx, CLIPRDR_FORMAT_LIST const* list) -> std::uint32_t {
    Expects(list != nullptr, "format list is supplied");
    return HeldClipboard(ctx).Formats(*ctx, *list);
  };
  context.ServerFormatListResponse = [](CliprdrClientContext* ctx,
                                        CLIPRDR_FORMAT_LIST_RESPONSE const*) -> std::uint32_t {
    return HeldClipboard(ctx).Accepted();
  };
}
// abi: the pcCliprdr server message callbacks, UINT is uint32_t
auto ClipboardClient::Callbacks::InstallData(CliprdrClientContext& context) -> void {
  context.ServerFormatDataRequest  = [](CliprdrClientContext* ctx,
                                        CLIPRDR_FORMAT_DATA_REQUEST const* request) -> std::uint32_t {
    Expects(request != nullptr, "data request is supplied");
    return HeldClipboard(ctx).Request(*ctx, *request);
  };
  context.ServerFormatDataResponse = [](CliprdrClientContext* ctx,
                                        CLIPRDR_FORMAT_DATA_RESPONSE const* response) -> std::uint32_t {
    Expects(response != nullptr, "data response is supplied");
    return HeldClipboard(ctx).Response(*response);
  };
}

ClipboardClient::ClipboardClient(Client& value, std::vector<std::uint8_t> initial)
    : client(value), outgoing(std::move(initial)) {
  ObserverSet::Of(*client.Instance()->context).Add(*this);
  freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
  auto* context = client.Instance()->context;
  Expects(freerdp_settings_set_bool(context->settings, FreeRDP_RedirectClipboard, true), "clipboard enabled");
  PubSub_SubscribeChannelConnected(context->pubSub, Callbacks::ChannelConnected);
  // abi: pLoadChannels, BOOL is int
  client.Instance()->LoadChannels = [](freerdp* instance) -> int {
    return LoadStaticChannel(instance, CLIPRDR_SVC_CHANNEL_NAME);
  };
}
ClipboardClient::~ClipboardClient() {
  client.Disconnect();
  PubSub_UnsubscribeChannelConnected(client.Instance()->context->pubSub, Callbacks::ChannelConnected);
  ObserverSet::Of(*client.Instance()->context).Remove<ClipboardClient>();
}
auto ClipboardClient::Received(std::vector<std::uint8_t> const& bytes) -> bool {
  std::scoped_lock const lock(guard);
  return incoming == bytes && std::ranges::contains(formats, CF_UNICODETEXT) && std::ranges::contains(formats, CF_TEXT);
}
auto ClipboardClient::RequestFormat(std::uint32_t format) -> std::uint32_t {
  Expects(channel.load(), "clipboard channel connected");
  CLIPRDR_FORMAT_DATA_REQUEST request{ .common = { .msgType = CB_FORMAT_DATA_REQUEST } };
  request.requestedFormatId = format;
  return channel.load()->ClientFormatDataRequest(channel.load(), &request);
}
auto ClipboardClient::Offer(std::vector<std::uint8_t> bytes, bool unicode) -> bool {
  Expects(channel.load(), "clipboard channel connected");
  {
    std::scoped_lock const lock(guard);
    outgoing = std::move(bytes);
  }
  return AnnounceFormat(*channel.load(), unicode) == CHANNEL_RC_OK;
}
auto ClipboardClient::Observed() const -> ClipboardCapture const& {
  return observed;
}
auto ClipboardClient::Accepted() -> std::uint32_t {
  ++observed.accepted;
  return CHANNEL_RC_OK;
}
auto ClipboardClient::Ready(CliprdrClientContext& context) -> std::uint32_t {
  auto result = Backend::SendGeneralCapabilities(
      [&](auto const* caps) { return context.ClientCapabilities(&context, caps); });
  if (result != CHANNEL_RC_OK) return result;
  if (!outgoing.empty()) return AnnounceFormat(context, true);
  CLIPRDR_FORMAT_LIST const list{ .common = { .msgType = CB_FORMAT_LIST } };
  return context.ClientFormatList(&context, &list);
}
auto ClipboardClient::Formats(CliprdrClientContext& context, CLIPRDR_FORMAT_LIST const& list) -> std::uint32_t {
  {
    std::scoped_lock const lock(guard);
    formats = std::span(list.formats, list.numFormats) | std::views::transform(&CLIPRDR_FORMAT::formatId)
              | std::ranges::to<std::vector>();
  }
  CLIPRDR_FORMAT_LIST_RESPONSE response{ .common = { .msgType = CB_FORMAT_LIST_RESPONSE } };
  response.common.msgFlags = CB_RESPONSE_OK;
  auto result = context.ClientFormatListResponse(&context, &response);
  if (result != CHANNEL_RC_OK) return result;
  CLIPRDR_FORMAT_DATA_REQUEST request{ .common = { .msgType = CB_FORMAT_DATA_REQUEST } };
  request.requestedFormatId = CF_UNICODETEXT;
  return context.ClientFormatDataRequest(&context, &request);
}
auto ClipboardClient::Request(CliprdrClientContext& context, CLIPRDR_FORMAT_DATA_REQUEST const& request)
    -> std::uint32_t {
  std::scoped_lock const       lock(guard);
  CLIPRDR_FORMAT_DATA_RESPONSE response{ .common = { .msgType = CB_FORMAT_DATA_RESPONSE } };
  response.common.msgFlags     = request.requestedFormatId == CF_UNICODETEXT ? CB_RESPONSE_OK : CB_RESPONSE_FAIL;
  response.common.dataLen      = outgoing.size();
  response.requestedFormatData = outgoing.data();
  ++observed.requests;
  return context.ClientFormatDataResponse(&context, &response);
}
auto ClipboardClient::Response(CLIPRDR_FORMAT_DATA_RESPONSE const& response) -> std::uint32_t {
  std::scoped_lock const lock(guard);
  if (response.common.msgFlags & CB_RESPONSE_OK)
    incoming.assign(response.requestedFormatData, response.requestedFormatData + response.common.dataLen);
  ++observed.responses;
  return CHANNEL_RC_OK;
}
}
