#include <sdl-rdp/headless-client.test/client/clipboard.hpp>

#include <sdl-rdp/clipboard/capabilities.hpp>
#include <sdl-rdp/headless-client.test/client/channels.hpp>
#include <sdl-rdp/headless-client.test/client/handles.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/addin.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/channels.h>
#include <oxbox/utilities/span.hpp>
#include <winpr/clipboard.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <utility>

namespace sdl_rdp::headless_client_test::client::detail::clipboard {
using sdl_rdp::clipboard::SendGeneralCapabilities;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;

namespace {
auto HeldClipboard(CliprdrClientContext& context) -> ClipboardClient& {
  Expects(context.custom != nullptr, "callback context carries its observer");
  return *static_cast<ClipboardClient*>(context.custom);
}
auto AnnounceFormat(CliprdrClientContext& context, bool unicode) -> std::uint32_t {
  CLIPRDR_FORMAT      format{ Narrowed<std::uint32_t>(unicode ? CF_UNICODETEXT : CF_DIB), nullptr };
  CLIPRDR_FORMAT_LIST list  { .common = { .msgType = CB_FORMAT_LIST }                             };
  list.numFormats = 1;
  list.formats    = &format;
  return context.ClientFormatList(&context, &list);
}
}
class ClipboardClient::Callbacks {
public:
  static auto InstallFormats(CliprdrClientContext& context) -> void;
  static auto InstallData(CliprdrClientContext& context)    -> void;
};
// abi: the pcCliprdr server message callbacks, UINT is uint32_t
auto ClipboardClient::Callbacks::InstallFormats(CliprdrClientContext& context) -> void {
  context.MonitorReady             = [](CliprdrClientContext* ctx, CLIPRDR_MONITOR_READY const*) -> std::uint32_t {
    Expects(ctx != nullptr, "the clipboard callback names its channel");
    return HeldClipboard(*ctx).Ready(*ctx);
  };
  context.ServerFormatList         = [](CliprdrClientContext* ctx, CLIPRDR_FORMAT_LIST const* list) -> std::uint32_t {
    Expects(ctx != nullptr, "the clipboard callback names its channel");
    Expects(list != nullptr, "format list is supplied");
    return HeldClipboard(*ctx).Formats(*ctx, *list);
  };
  context.ServerFormatListResponse = [](CliprdrClientContext* ctx,
                                        CLIPRDR_FORMAT_LIST_RESPONSE const*) -> std::uint32_t {
    Expects(ctx != nullptr, "the clipboard callback names its channel");
    return HeldClipboard(*ctx).Accepted();
  };
}
// abi: the pcCliprdr server message callbacks, UINT is uint32_t
auto ClipboardClient::Callbacks::InstallData(CliprdrClientContext& context) -> void {
  context.ServerFormatDataRequest  = [](CliprdrClientContext* ctx,
                                        CLIPRDR_FORMAT_DATA_REQUEST const* request) -> std::uint32_t {
    Expects(ctx != nullptr, "the clipboard callback names its channel");
    Expects(request != nullptr, "data request is supplied");
    return HeldClipboard(*ctx).Request(*ctx, *request);
  };
  context.ServerFormatDataResponse = [](CliprdrClientContext* ctx,
                                        CLIPRDR_FORMAT_DATA_RESPONSE const* response) -> std::uint32_t {
    Expects(ctx != nullptr, "the clipboard callback names its channel");
    Expects(response != nullptr, "data response is supplied");
    return HeldClipboard(*ctx).Response(*response);
  };
}

ClipboardClient::ClipboardClient(Client& value, std::vector<std::byte> initial)
    : client(value), outgoing(std::move(initial)), membership(ClientContext(client), *this),
      connections(ClientContext(client)) {
  freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
  auto const redirected = freerdp_settings_set_bool(ClientContext(client).settings, FreeRDP_RedirectClipboard, true);
  Expects(redirected, "clipboard enabled");
  ClientHandle(client).LoadChannels = ChannelLoader<LoadStaticChannel, CLIPRDR_SVC_CHANNEL_NAME>;
}
ClipboardClient::~ClipboardClient() {
  client.Disconnect();
}
auto ClipboardClient::Attach(CliprdrClientContext& context) -> void {
  context.custom = this;
  Callbacks::InstallFormats(context);
  Callbacks::InstallData(context);
  channel.Publish(context);
}
auto ClipboardClient::Received(std::span<std::byte const> bytes) -> bool {
  std::scoped_lock const lock(guard);
  return std::ranges::equal(incoming, bytes) && std::ranges::contains(formats, CF_UNICODETEXT)
         && std::ranges::contains(formats, CF_TEXT);
}
auto ClipboardClient::RequestFormat(std::uint32_t format) -> std::uint32_t {
  auto&                       connected = channel.Get();
  CLIPRDR_FORMAT_DATA_REQUEST request   { .common = { .msgType = CB_FORMAT_DATA_REQUEST } };
  request.requestedFormatId = format;
  return connected.ClientFormatDataRequest(&connected, &request);
}
auto ClipboardClient::Offer(std::span<std::byte const> bytes, bool unicode) -> bool {
  auto& connected = channel.Get();
  {
    std::scoped_lock const lock(guard);
    outgoing.assign(bytes.begin(), bytes.end());
  }
  return AnnounceFormat(connected, unicode) == CHANNEL_RC_OK;
}
auto ClipboardClient::Observed() const -> ClipboardCapture const& {
  return observed;
}
auto ClipboardClient::Accepted() -> std::uint32_t {
  ++observed.accepted;
  return CHANNEL_RC_OK;
}
auto ClipboardClient::Ready(CliprdrClientContext& context) -> std::uint32_t {
  auto result = SendGeneralCapabilities([&](auto const& caps) { return context.ClientCapabilities(&context, &caps); });
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
  response.requestedFormatData = oxbox::utilities::SpanCast<std::uint8_t const>(std::span(outgoing)).data();
  ++observed.requests;
  return context.ClientFormatDataResponse(&context, &response);
}
auto ClipboardClient::Response(CLIPRDR_FORMAT_DATA_RESPONSE const& response) -> std::uint32_t {
  std::scoped_lock const lock(guard);
  if (response.common.msgFlags & CB_RESPONSE_OK) {
    auto const data = std::as_bytes(std::span(response.requestedFormatData, response.common.dataLen));
    incoming.assign(data.begin(), data.end());
  }
  ++observed.responses;
  return CHANNEL_RC_OK;
}
}
